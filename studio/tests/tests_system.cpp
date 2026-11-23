// Tests: sha256/pbkdf2, files, accounts, profile, extensions, runner.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <thread>
#include "accounts.hpp"
#include "extensions.hpp"
#include "files.hpp"
#include "profile.hpp"
#include "runner.hpp"
#include "scan.hpp"
#include "semantic/semantic.hpp"
#include <map>
#include "sha256.hpp"
#include "test_util.hpp"

using namespace studio;
namespace fs = std::filesystem;

static std::string hexOf(const Digest& d) { return toHex(d.data(), d.size()); }
static std::string slurp(const fs::path& p) { std::string s; files::readText(p, s); return s; }
static void spit(const fs::path& p, const std::string& s) { files::writeTextAtomic(p, s); }

TEST(sha256_and_pbkdf2_known_vectors) {
    auto h = [](const std::string& s) { return hexOf(sha256(reinterpret_cast<const std::uint8_t*>(s.data()), s.size())); };
    CHECK_EQ(h("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK_EQ(h(""), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK_EQ(h(std::string(1000, 'a')), std::string("41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3"));
    std::string msg = "what do ya want for nothing?";
    std::vector<std::uint8_t> key = {'J', 'e', 'f', 'e'};
    CHECK_EQ(hexOf(hmacSha256(key, reinterpret_cast<const std::uint8_t*>(msg.data()), msg.size())), std::string("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"));
    std::vector<std::uint8_t> salt = {'s', 'a', 'l', 't'};
    auto k1 = pbkdf2Sha256("password", salt, 1), k2 = pbkdf2Sha256("password", salt, 2), k4096 = pbkdf2Sha256("password", salt, 4096);
    CHECK_EQ(toHex(k1.data(), 32), std::string("120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b"));
    CHECK_EQ(toHex(k2.data(), 32), std::string("ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43"));
    CHECK_EQ(toHex(k4096.data(), 32), std::string("c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a"));
    std::vector<std::uint8_t> back; CHECK(fromHex("00ff10", back)); CHECK_EQ(back.size(), static_cast<size_t>(3)); CHECK(!fromHex("0g", back)); CHECK(!fromHex("abc", back));
    CHECK(constantTimeEqual({1, 2}, {1, 2})); CHECK(!constantTimeEqual({1, 2}, {1, 3})); CHECK(!constantTimeEqual({1}, {1, 2}));
}

TEST(files_atomic_write_listing_unique_names) {
    fs::path d = T::tempDir("files");
    std::string err;
    CHECK(files::writeTextAtomic(d / "sub" / "a.pcs", "one", &err));          // creates the folder
    CHECK(files::writeTextAtomic(d / "sub" / "a.pcs", "two", &err));          // overwrites
    CHECK_EQ(slurp(d / "sub" / "a.pcs"), std::string("two"));
    CHECK(!fs::exists(d / "sub" / "a.pcs.tmp"));
    std::string out; CHECK(!files::readText(d / "missing.pcs", out, &err)); CHECK(err.find("does not exist") != std::string::npos);
    CHECK(!files::readText(d / "sub", out, &err));
    spit(d / "bom.pcs", "\xEF\xBB\xBFhi"); CHECK(files::readText(d / "bom.pcs", out)); CHECK_EQ(out, std::string("hi"));
    CHECK_EQ(files::uniqueFileName(d, "Untitled", ".pcs"), std::string("Untitled1.pcs"));
    spit(d / "Untitled1.pcs", ""); spit(d / "Untitled2.pcs", "");
    CHECK_EQ(files::uniqueFileName(d, "Untitled", ".pcs"), std::string("Untitled3.pcs"));
    spit(d / ".hidden", ""); spit(d / "Zed.pcs", ""); spit(d / "apple.pcs", "");
    auto l = files::listDir(d);
    CHECK(!l.empty()); CHECK(l[0].isDir); CHECK_EQ(l[0].name, std::string("sub"));
    bool hidden = false; for (auto& e : l) if (e.name == ".hidden") hidden = true;
    CHECK(!hidden);
    CHECK(files::hasExtension("x/Prog.PCS", ".pcs"));
    // non-ASCII file names survive the UTF-8 conversion
    std::string uni = "caf\xC3\xA9_\xE2\x86\x90.pcs";
    CHECK(files::writeTextAtomic(d / files::fromUtf8(uni), "x")); CHECK_EQ(files::toUtf8(files::fromUtf8(uni)), uni); CHECK(fs::exists(d / files::fromUtf8(uni)));
    std::error_code ec; fs::remove_all(d, ec);
}

TEST(accounts_create_login_persist) {
    fs::path d = T::tempDir("accounts");
    AccountStore s(d, 50);
    std::string err;
    CHECK(s.load(&err));                                      // no file yet is fine
    CHECK(s.usernames().empty()); CHECK(!s.signedIn());
    CHECK(s.create("Alice", "secret1", "Alice A.", &err));
    CHECK(!s.create("alice", "secret2", "", &err)); CHECK(err.find("taken") != std::string::npos);          // case-insensitive uniqueness
    CHECK(!s.create("ab", "secret1", "", &err));                                                              // too short
    CHECK(!s.create("bad name", "secret1", "", &err));
    CHECK(!s.create("Guest", "secret1", "", &err));                                                           // reserved
    CHECK(!s.create("CON", "secret1", "", &err));
    CHECK(!s.create("../evil", "secret1", "", &err));
    CHECK(!s.create("Bobby", "123", "", &err));                                                               // password too short
    CHECK(s.create("Bobby", "pa55word", "", &err));
    CHECK(s.verify("ALICE", "secret1")); CHECK(!s.verify("alice", "Secret1")); CHECK(!s.verify("alice", "")); CHECK(!s.verify("nobody", "secret1"));
    CHECK(!s.login("alice", "wrong", &err)); CHECK(!s.signedIn()); CHECK_EQ(err, std::string("Wrong user name or password."));
    CHECK(s.login("alice", "secret1")); CHECK(s.signedIn()); CHECK_EQ(s.currentUser(), std::string("Alice")); CHECK_EQ(s.currentDisplayName(), std::string("Alice A."));
    CHECK(slurp(d / "accounts.json").find("secret1") == std::string::npos);                                   // password is never stored
    // survives a restart
    AccountStore t(d, 50);
    CHECK(t.load()); CHECK_EQ(t.usernames().size(), static_cast<size_t>(2)); CHECK(t.verify("alice", "secret1")); CHECK(t.verify("bobby", "pa55word"));
    // password change
    CHECK(!t.changePassword("alice", "bad", "newsecret", &err)); CHECK(!t.changePassword("alice", "secret1", "x", &err));
    CHECK(t.changePassword("alice", "secret1", "newsecret")); CHECK(!t.verify("alice", "secret1")); CHECK(t.verify("alice", "newsecret"));
    AccountStore u(d, 50); u.load(); CHECK(u.verify("alice", "newsecret"));
    // guest and logout
    u.loginGuest(); CHECK(u.isGuest()); CHECK(u.signedIn()); CHECK_EQ(u.currentUser(), std::string("Guest")); u.logout(); CHECK(!u.signedIn());
    CHECK(u.profileDir("alice") == d / "users" / "Alice"); CHECK(u.profileDir("") == d / "users" / "Guest");
    // removal keeps the person's files
    fs::create_directories(u.profileDir("Bobby")); spit(u.profileDir("Bobby") / "keep.txt", "x");
    CHECK(!u.remove("bobby", "wrong", &err)); CHECK(u.remove("bobby", "pa55word")); CHECK(u.find("bobby") == nullptr);
    CHECK(fs::exists(u.profileDir("Bobby") / "keep.txt"));
    AccountStore v(d, 50); v.load(); CHECK_EQ(v.usernames().size(), static_cast<size_t>(1));
    // a damaged accounts file is reported and left alone
    spit(d / "accounts.json", "{ not json");
    AccountStore w(d, 50); CHECK(!w.load(&err)); CHECK(err.find("damaged") != std::string::npos); CHECK_EQ(slurp(d / "accounts.json"), std::string("{ not json"));
    std::error_code ec; fs::remove_all(d, ec);
}

TEST(accounts_hashing_cost) {
    fs::path d = T::tempDir("accounts_cost");
    AccountStore s(d);   // production iteration count
    auto t0 = std::chrono::steady_clock::now();
    CHECK(s.create("Timing", "password1", ""));
    CHECK(s.verify("Timing", "password1"));
    long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "   (create + verify with the production PBKDF2 setting took " << ms << " ms in this build)\n";
    CHECK(ms < 8000);
    std::error_code ec; fs::remove_all(d, ec);
}

TEST(profile_settings_session_recovery_themes) {
    fs::path d = T::tempDir("profile");
    UserProfile p(d / "alice");
    std::string warn;
    CHECK(p.load(&warn)); CHECK(warn.empty()); CHECK(p.settings == Settings{}); CHECK(p.session.tabs.empty());
    p.settings.fontSize = 20; p.settings.theme = "Midnight"; p.settings.tabSize = 2;
    p.session.tabs.push_back({"C:\\x\\a.pcs", "", 3, 4, 1}); p.session.tabs.push_back({"", "rec1", 0, 0, 0}); p.session.activeTab = 1;
    p.session.window.w = 1000; p.session.window.maximized = true; p.session.explorerRoot = "D:\\progs";
    p.disabledExtensions.insert("pcs.theme.ocean");
    for (int i = 0; i < 20; ++i) p.addRecent("file" + std::to_string(i) + ".pcs");
    p.addRecent("FILE5.pcs");                                            // moves to the front, case-insensitive duplicate removed
    CHECK_EQ(p.session.recentFiles.size(), static_cast<size_t>(12)); CHECK_EQ(p.session.recentFiles[0], std::string("FILE5.pcs"));
    std::string err; CHECK(p.save(&err));
    UserProfile q(d / "alice"); CHECK(q.load(&warn));
    CHECK_EQ(q.settings.fontSize, 20); CHECK_EQ(q.settings.theme, std::string("Midnight")); CHECK_EQ(q.settings.tabSize, 2);
    CHECK_EQ(q.session.tabs.size(), static_cast<size_t>(2)); CHECK_EQ(q.session.tabs[0].line, 3); CHECK_EQ(q.session.tabs[1].recoveryId, std::string("rec1"));
    CHECK_EQ(q.session.activeTab, 1); CHECK(q.session.window.maximized); CHECK_EQ(q.session.window.w, 1000); CHECK_EQ(q.session.explorerRoot, std::string("D:\\progs"));
    CHECK(q.disabledExtensions.count("pcs.theme.ocean")); CHECK_EQ(q.session.recentFiles.size(), static_cast<size_t>(12));
    // damaged files: warn, fall back to defaults, never throw
    spit(d / "alice" / "settings.json", "{{{"); spit(d / "alice" / "session.json", "nope");
    UserProfile r(d / "alice"); warn.clear(); CHECK(r.load(&warn)); CHECK(!warn.empty()); CHECK(r.settings == Settings{}); CHECK(r.session.tabs.empty());
    // crash recovery
    RecoveryItem item; item.id = UserProfile::newRecoveryId(); item.title = "Untitled1.pcs"; item.content = "OUTPUT \"unsaved \xE2\x86\x90\"\n";
    CHECK(q.saveRecovery(item, &err));
    auto rec = q.loadRecovery(); CHECK_EQ(rec.size(), static_cast<size_t>(1)); CHECK_EQ(rec[0].content, item.content); CHECK_EQ(rec[0].id, item.id);
    q.clearRecovery(item.id); CHECK(q.loadRecovery().empty());
    // user themes
    Theme t = builtinThemes()[0]; t.name = "My Theme / Test"; t[Slot::Keyword] = 0x123456;
    CHECK(q.saveUserTheme(t, &err));
    spit(q.themesDir() / "broken.json", "{"); spit(q.themesDir() / "noname.json", "{\"colors\":{}}");
    std::vector<std::string> errs; auto themes = q.loadUserThemes(&errs);
    CHECK_EQ(themes.size(), static_cast<size_t>(1)); CHECK_EQ(themes[0].name, std::string("My Theme / Test")); CHECK_EQ(themes[0][Slot::Keyword], 0x123456u); CHECK_EQ(errs.size(), static_cast<size_t>(2));
    std::error_code ec; fs::remove_all(d, ec);
}

// ------------------------------------------------------------------ extensions
static const fs::path kExt = PCS_EXTENSIONS_DIR;

TEST(extensions_bundled_load) {
    ExtensionManager m(kExt, T::tempDir("ext_user_a"));
    m.scan();
    CHECK_EQ(m.all().size(), static_cast<size_t>(3));
    for (const Extension& e : m.all()) { CHECK(e.problems.empty()); CHECK(e.builtin); CHECK(e.enabled); if (!e.problems.empty()) std::cout << "   " << e.id << ": " << e.problems[0] << "\n"; }
    auto th = m.themes();
    CHECK_EQ(th.size(), static_cast<size_t>(2));
    CHECK_EQ(th[0].name, std::string("Ocean Deep")); CHECK(th[0].dark); CHECK_EQ(th[0][Slot::Keyword], 0x5CC8FFu);
    CHECK_EQ(th[1].name, std::string("Sea Glass")); CHECK(!th[1].dark);
    CHECK_EQ(th[1][Slot::Operator], builtinThemes()[1][Slot::Operator]);            // un-overridden colour comes from the light base
    CHECK_EQ(m.snippets().size(), static_cast<size_t>(11));
    CHECK_EQ(m.templates().size(), static_cast<size_t>(4));
    m.setEnabled("pcs.theme.ocean", false);
    CHECK(m.themes().empty()); CHECK_EQ(m.snippets().size(), static_cast<size_t>(11));
    m.setDisabled({"pcs.snippets.cambridge"}); CHECK(m.snippets().empty()); CHECK_EQ(m.themes().size(), static_cast<size_t>(2));
}

static std::string runToString(const std::string& src, const std::string& input = "", const fs::path& base = {});

TEST(extensions_snippets_are_real_working_code) {
    ExtensionManager m(kExt, T::tempDir("ext_user_b")); m.scan();
    std::string all;
    for (const Snippet& s : m.snippets()) {
        CHECK(s.body.find("$0") == std::string::npos);
        CHECK(!checkSource(s.body, "snippet.pcs").empty() ? !pcs::hasErrors(checkSource(s.body, "snippet.pcs")) : true);
        all += s.body + "\n";
    }
    std::string prog = all +
        "DECLARE Marks : ARRAY[1:5] OF INTEGER\nDECLARE i : INTEGER\nDECLARE S : Stack\n"
        "Marks[1] <- 5\nMarks[2] <- 3\nMarks[3] <- 9\nMarks[4] <- 1\nMarks[5] <- 7\n"
        "OUTPUT LinearSearch(Marks, 5, 9)\n"
        "CALL BubbleSort(Marks, 5)\nFOR i <- 1 TO 5\n    OUTPUT Marks[i]\nNEXT i\n"
        "OUTPUT BinarySearch(Marks, 5, 7)\n"
        "Marks[1] <- 4\nMarks[2] <- 2\nMarks[3] <- 5\nMarks[4] <- 1\nMarks[5] <- 3\nCALL InsertionSort(Marks, 5)\nOUTPUT Marks[1], Marks[2], Marks[3], Marks[4], Marks[5]\n"
        "OUTPUT Factorial(5), \" \", Fibonacci(10), \" \", IsPrime(13), \" \", IsPrime(15), \" \", GCD(48, 18)\n"
        "OUTPUT ReverseString(\"abc\"), \" \", IsPalindrome(\"level\"), \" \", IsPalindrome(\"hello\")\n"
        "S <- NEW Stack()\nCALL S.Push(1)\nCALL S.Push(2)\nOUTPUT S.Pop(), S.Pop(), S.IsEmpty()\n";
    CHECK_EQ(runToString(prog), std::string("3\n1\n3\n5\n7\n9\n4\n12345\n120 55 TRUE FALSE 6\ncba TRUE FALSE\n21TRUE\n"));
}

TEST(extensions_templates_run) {
    ExtensionManager m(kExt, T::tempDir("ext_user_c")); m.scan();
    fs::path work = T::tempDir("tmpl_work");
    std::map<std::string, std::string> byName; for (auto& t : m.templates()) byName[t.name] = t.body;
    CHECK_EQ(byName.size(), static_cast<size_t>(4));
    CHECK_EQ(runToString(byName["Blank program"]), std::string("Hello, World!\n"));
    std::string menu = runToString(byName["Menu program"], "1\n2\n7\n3\n");
    CHECK(menu.find("Hello!") != std::string::npos && menu.find("Goodbye!") != std::string::npos && menu.find("Please enter 1, 2 or 3") != std::string::npos && menu.find("Quitting...") != std::string::npos);
    std::string val = runToString(byName["Input validation"], "0\n11\n4\n");
    CHECK(val.find("Thank you. You entered 4") != std::string::npos);
    CHECK_EQ(runToString(byName["Text file demo"], "", work), std::string("First line\nSecond line\nThird line\n"));
    CHECK(fs::exists(work / "notes.txt"));
    std::error_code ec; fs::remove_all(work, ec);
}

static void writeExt(const fs::path& dir, const std::string& manifest) { fs::create_directories(dir); spit(dir / "extension.json", manifest); }

TEST(extensions_install_uninstall_and_safety) {
    fs::path user = T::tempDir("ext_user_d"), src = T::tempDir("ext_src");
    ExtensionManager m(kExt, user); m.scan();
    std::string err;
    // path traversal in a manifest is refused, the rest of the extension still loads
    spit(src / "secret.json", R"({"name":"Stolen","base":"dark"})");
    writeExt(src / "evil", R"({"id":"evil.ext","name":"Evil","contributes":{"themes":[{"file":"../secret.json"},{"file":"/etc/passwd"}],
        "snippets":[{"prefix":"ok","body":"OUTPUT 1"},{"prefix":"bad prefix","body":"x"},{"prefix":"nobody"}]}})");
    Extension x; CHECK(ExtensionManager::loadManifest(src / "evil", x, &err));
    CHECK(x.themes.empty()); CHECK_EQ(x.snippets.size(), static_cast<size_t>(1)); CHECK_EQ(x.problems.size(), static_cast<size_t>(4));
    // invalid manifests are rejected
    writeExt(src / "noid", R"({"name":"X"})"); CHECK(!ExtensionManager::loadManifest(src / "noid", x, &err));
    writeExt(src / "badid", R"({"id":"Has Spaces","name":"X"})"); CHECK(!ExtensionManager::loadManifest(src / "badid", x, &err));
    writeExt(src / "badjson", "{"); CHECK(!ExtensionManager::loadManifest(src / "badjson", x, &err)); CHECK(err.find("JSON") != std::string::npos);
    CHECK(!m.install(src / "badjson", &err)); CHECK(!m.install(src / "nonexistent", &err));
    // a good one installs, is listed, can be disabled and removed
    writeExt(src / "good", R"({"id":"my.good","name":"Good","version":"2.0","contributes":{"themes":[{"name":"Inline One","base":"light","colors":{"keyword":"#FF0000"}}],
        "templates":[{"name":"T","body":"OUTPUT 1\n"}]}})");
    CHECK(m.install(src / "good", &err));
    CHECK_EQ(m.all().size(), static_cast<size_t>(4));
    bool found = false; for (auto& e : m.all()) if (e.id == "my.good") { found = true; CHECK(!e.builtin); CHECK_EQ(e.version, std::string("2.0")); }
    CHECK(found); CHECK_EQ(m.themes().size(), static_cast<size_t>(3));
    ExtensionManager again(kExt, user); again.scan(); CHECK_EQ(again.all().size(), static_cast<size_t>(4));      // persisted
    CHECK(!m.uninstall("pcs.theme.ocean", &err)); CHECK(err.find("Built-in") != std::string::npos);                // built-ins stay
    CHECK(!m.uninstall("nope", &err));
    CHECK(m.uninstall("my.good", &err)); CHECK_EQ(m.all().size(), static_cast<size_t>(3));
    // cannot shadow a built-in id
    writeExt(src / "shadow", R"({"id":"pcs.theme.ocean","name":"Fake"})"); CHECK(!m.install(src / "shadow", &err));
    // a broken folder inside the user dir shows up as a disabled, explained entry instead of crashing the scan
    fs::create_directories(user / "junk"); m.scan();
    bool sawInvalid = false; for (auto& e : m.all()) if (e.id.rfind("invalid:", 0) == 0) { sawInvalid = true; CHECK(!e.enabled); CHECK(!e.problems.empty()); }
    CHECK(sawInvalid);
    std::error_code ec; fs::remove_all(user, ec); fs::remove_all(src, ec);
}

// ------------------------------------------------------------------ runner
struct Wait {
    std::mutex m; std::condition_variable cv; bool done = false; RunResult res; std::string out; int inputRequests = 0;
    bool waitDone(int ms = 8000) { std::unique_lock<std::mutex> l(m); return cv.wait_for(l, std::chrono::milliseconds(ms), [&] { return done; }); }
    bool waitInput(int n, int ms = 8000) { std::unique_lock<std::mutex> l(m); return cv.wait_for(l, std::chrono::milliseconds(ms), [&] { return inputRequests >= n; }); }
};
static RunEvents eventsFor(Wait& w, std::function<void()> onInput = nullptr) {
    RunEvents e;
    e.onOutput = [&w](const std::string& s) { std::lock_guard<std::mutex> l(w.m); w.out += s; };
    e.onInputRequested = [&w, onInput]() { { std::lock_guard<std::mutex> l(w.m); ++w.inputRequests; } w.cv.notify_all(); if (onInput) onInput(); };
    e.onFinished = [&w](const RunResult& r) { { std::lock_guard<std::mutex> l(w.m); w.res = r; w.done = true; } w.cv.notify_all(); };
    return e;
}
static std::string runToString(const std::string& src, const std::string& input, const fs::path& base) {
    Runner r; Wait w; RunRequest q; q.source = src; q.fileName = "t.pcs"; q.baseDir = base.empty() ? "" : files::toUtf8(base); q.hasSeed = true; q.seed = 1; q.timeoutMs = 10000;
    std::vector<std::string> lines = splitLines(input); if (!lines.empty() && lines.back().empty()) lines.pop_back();
    size_t next = 0; Runner* rp = &r;
    RunEvents e = eventsFor(w, [&]() { if (next < lines.size()) rp->provideInput(lines[next++]); else rp->endInput(); });
    r.start(q, e);
    if (!w.waitDone()) { r.stop(); return "<<timeout>>"; }
    r.wait();
    if (w.res.status != RunStatus::Completed) return "<<" + std::to_string(static_cast<int>(w.res.status)) + (w.res.diagnostics.empty() ? "" : ":" + w.res.diagnostics.back().code) + ">>" + w.out;
    return w.out;
}

TEST(runner_output_input_and_errors) {
    CHECK_EQ(runToString("OUTPUT \"Hello\"\nOUTPUT 1 + 2"), std::string("Hello\n3\n"));
    CHECK_EQ(runToString("DECLARE a, b : INTEGER\nINPUT a\nINPUT b\nOUTPUT a + b", "20\n22\n"), std::string("42\n"));
    CHECK_EQ(runToString("DECLARE s : STRING\nOUTPUT \"Name?\"\nINPUT s\nOUTPUT \"Hi \" & s", "Zara\n"), std::string("Name?\nHi Zara\n"));
    CHECK_EQ(runToString("OUTPUT 1\nOUTPUT 1 DIV 0\nOUTPUT 2"), std::string("<<2:R301>>1\n"));                       // output before the error is kept
    CHECK_EQ(runToString("OUTPUT x"), std::string("<<1:E201>>"));                                                    // compile error: nothing runs
    CHECK_EQ(runToString("IF TRUE THEN\nOUTPUT 1"), std::string("<<1:E103>>"));                                       // syntax error
    CHECK_EQ(runToString("DECLARE a : INTEGER\nINPUT a\nOUTPUT a", ""), std::string("<<2:R309>>"));                   // ran out of input
    // warnings do not stop a program, and are reported
    Runner r; Wait w; RunRequest q; q.source = "DECLARE unused : INTEGER\nOUTPUT 5"; q.fileName = "w.pcs";
    r.start(q, eventsFor(w)); CHECK(w.waitDone()); r.wait();
    CHECK(w.res.status == RunStatus::Completed); CHECK_EQ(w.out, std::string("5\n")); bool w501 = false; for (auto& d : w.res.diagnostics) if (d.code == "W501") w501 = true; CHECK(w501);
    CHECK(!r.running());
}

TEST(runner_streams_output_before_finishing) {
    // the first lines must reach the console while the program is still waiting for input
    Runner r; Wait w; RunRequest q; q.source = "OUTPUT \"prompt\"\nDECLARE x : INTEGER\nINPUT x\nOUTPUT x * 2"; q.fileName = "s.pcs";
    r.start(q, eventsFor(w));
    CHECK(w.waitInput(1));
    { std::lock_guard<std::mutex> l(w.m); CHECK_EQ(w.out, std::string("prompt\n")); CHECK(!w.done); }
    CHECK(r.running());
    r.provideInput("21");
    CHECK(w.waitDone()); r.wait();
    CHECK_EQ(w.out, std::string("prompt\n42\n"));
    // input typed before it is asked for is queued
    Wait w2; r.start(q, eventsFor(w2)); r.provideInput("5"); CHECK(w2.waitDone()); r.wait(); CHECK_EQ(w2.out, std::string("prompt\n10\n"));
}

TEST(runner_stop_timeout_and_reuse) {
    Runner r;
    {   // an infinite loop is stopped by the Stop button
        Wait w; RunRequest q; q.source = "WHILE TRUE DO\nENDWHILE"; q.fileName = "loop.pcs";
        CHECK(r.start(q, eventsFor(w)));
        CHECK(r.running()); CHECK(!r.start(q, eventsFor(w)));                       // cannot start twice
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        auto t0 = std::chrono::steady_clock::now(); r.stop();
        CHECK(w.waitDone(5000)); r.wait();
        CHECK(w.res.status == RunStatus::Stopped);
        CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(3));
        CHECK(!r.running());
    }
    {   // stop while the program waits for INPUT
        Wait w; RunRequest q; q.source = "DECLARE x : INTEGER\nINPUT x\nOUTPUT x"; q.fileName = "in.pcs";
        r.start(q, eventsFor(w)); CHECK(w.waitInput(1)); r.stop(); CHECK(w.waitDone(5000)); r.wait(); CHECK(w.res.status == RunStatus::Stopped);
    }
    {   // timeout
        Wait w; RunRequest q; q.source = "WHILE TRUE DO\nENDWHILE"; q.fileName = "t.pcs"; q.timeoutMs = 200;
        r.start(q, eventsFor(w)); CHECK(w.waitDone(8000)); r.wait();
        CHECK(w.res.status == RunStatus::RuntimeError); CHECK_EQ(w.res.diagnostics.back().code, std::string("R310"));
    }
    {   // after all that the runner still works
        Wait w; RunRequest q; q.source = "OUTPUT \"again\""; q.fileName = "ok.pcs";
        CHECK(r.start(q, eventsFor(w))); CHECK(w.waitDone()); r.wait(); CHECK_EQ(w.out, std::string("again\n")); CHECK(w.res.status == RunStatus::Completed);
    }
#if !defined(__SANITIZE_ADDRESS__)
    {   // deep recursion has enough stack (the interpreter would crash on a small thread stack)
        Wait w; RunRequest q; q.source = "FUNCTION F(N : INTEGER) RETURNS INTEGER\n    IF N = 0 THEN\n        RETURN 0\n    ENDIF\n    RETURN 1 + F(N - 1)\nENDFUNCTION\nOUTPUT F(4500)\nOUTPUT F(100000)"; q.fileName = "deep.pcs";
        r.start(q, eventsFor(w)); CHECK(w.waitDone(20000)); r.wait();
        CHECK_EQ(w.out, std::string("4500\n")); CHECK(w.res.status == RunStatus::RuntimeError); CHECK_EQ(w.res.diagnostics.back().code, std::string("R307"));
    }
#endif
}

TEST(check_source_for_live_diagnostics) {
    CHECK(checkSource("OUTPUT 1", "a.pcs").empty());
    auto d = checkSource("DECLARE x : INTEGER\nx <- \"a\"", "a.pcs");
    CHECK(!d.empty()); CHECK_EQ(d[0].code, std::string("E203")); CHECK_EQ(d[0].line, 2);
    d = checkSource("IF TRUE THEN", "a.pcs"); CHECK_EQ(d.size(), static_cast<size_t>(1)); CHECK_EQ(d[0].code, std::string("E103"));
    d = checkSource("x @ y", "a.pcs"); CHECK_EQ(d.size(), static_cast<size_t>(1)); CHECK_EQ(d[0].code, std::string("E101"));
    d = checkSource(std::string("OUTPUT \"a\0b\"", 12), "a.pcs"); CHECK(d.empty() || !d.empty());      // embedded NUL must not crash
}
