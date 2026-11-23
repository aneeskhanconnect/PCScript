// Command and control identifiers shared by the GUI sources and the resource script.
#pragma once

#define IDI_APP 101
#define IDI_PCS_FILE 102

// ---- commands (menus, toolbar, shortcuts) ----
#define IDM_FILE_NEW            1001
#define IDM_FILE_OPEN           1002
#define IDM_FILE_SAVE           1003
#define IDM_FILE_SAVEAS         1004
#define IDM_FILE_SAVEALL        1005
#define IDM_FILE_CLOSE          1006
#define IDM_FILE_CLOSEALL       1007
#define IDM_FILE_EXIT           1008
#define IDM_FILE_OPENFOLDER     1009

#define IDM_EDIT_UNDO           1101
#define IDM_EDIT_REDO           1102
#define IDM_EDIT_CUT            1103
#define IDM_EDIT_COPY           1104
#define IDM_EDIT_PASTE          1105
#define IDM_EDIT_SELECTALL      1106
#define IDM_EDIT_FIND           1107
#define IDM_EDIT_REPLACE        1108
#define IDM_EDIT_GOTO           1109
#define IDM_EDIT_FORMAT         1110
#define IDM_EDIT_COMMENT        1111
#define IDM_EDIT_DUPLICATE      1112
#define IDM_EDIT_DELETELINE     1113
#define IDM_EDIT_MOVEUP         1114
#define IDM_EDIT_MOVEDOWN       1115
#define IDM_EDIT_INDENT         1116
#define IDM_EDIT_OUTDENT        1117
#define IDM_EDIT_SUGGEST        1118
#define IDM_EDIT_FINDNEXT       1119
#define IDM_EDIT_FINDPREV       1120

#define IDM_VIEW_EXPLORER       1201
#define IDM_VIEW_OUTPUT         1202
#define IDM_VIEW_STATUSBAR      1203
#define IDM_VIEW_TOOLBAR        1204
#define IDM_VIEW_ZOOMIN         1205
#define IDM_VIEW_ZOOMOUT        1206
#define IDM_VIEW_ZOOMRESET      1207
#define IDM_VIEW_LINENUMBERS    1208
#define IDM_VIEW_INDENTGUIDES   1209
#define IDM_VIEW_WHITESPACE     1210
#define IDM_VIEW_PROBLEMS       1211
#define IDM_VIEW_CHOOSEFOLDER   1212

#define IDM_RUN_RUN             1301
#define IDM_RUN_STOP            1302
#define IDM_RUN_CHECK           1303
#define IDM_RUN_CLEAR           1304

#define IDM_ACCOUNT_SWITCH      1401
#define IDM_ACCOUNT_CREATE      1402
#define IDM_ACCOUNT_PASSWORD    1403
#define IDM_ACCOUNT_DELETE      1404

#define IDM_TOOLS_SETTINGS      1501
#define IDM_TOOLS_EXTENSIONS    1502
#define IDM_TOOLS_DATAFOLDER    1503
#define IDM_TOOLS_THEMESFOLDER  1504

#define IDM_HELP_REFERENCE      1601
#define IDM_HELP_CAMBRIDGE      1602
#define IDM_HELP_SHORTCUTS      1603
#define IDM_HELP_ABOUT          1604
#define IDM_HELP_IDEGUIDE       1605
#define IDM_TAB_NEXT            1701
#define IDM_TAB_PREV            1702

// Dynamic menu ranges
#define IDM_RECENT_BASE         2000   // 2000..2011
#define IDM_TEMPLATE_BASE       2100   // 2100..2139
#define IDM_THEME_BASE          2200   // 2200..2259
#define IDM_EXAMPLE_BASE        2300   // 2300..2359

// Context-menu commands of the explorer
#define IDM_EXPL_NEWFILE        2401
#define IDM_EXPL_REVEAL         2402
#define IDM_EXPL_REFRESH        2403
#define IDM_EXPL_OPEN           2404

// Child window / control ids of the main window
#define IDC_TOOLBAR             3001
#define IDC_TABS                3002
#define IDC_EXPLORER            3003
#define IDC_EDITOR              3004
#define IDC_CONSOLE             3005
#define IDC_INPUT               3006
#define IDC_PROBLEMS            3007
#define IDC_STATUS              3008
#define IDC_SENDINPUT           3009

// Timers
#define TIMER_CHECK             1
#define TIMER_AUTOSAVE          2
#define TIMER_RECOVERY          3

// Private window messages
#define WM_PCS_OUTPUT           (0x8000 + 10)   // worker -> UI: new program output is waiting in the buffer
#define WM_PCS_INPUT_REQUEST    (0x8000 + 11)   // worker -> UI: the program is waiting for INPUT
#define WM_PCS_FINISHED         (0x8000 + 12)   // worker -> UI: the program has ended
#define WM_PCS_EDITOR           (0x8000 + 20)   // editor -> parent: wParam = EditorEvent
#define WM_PCS_TABS             (0x8000 + 21)   // tab bar -> parent: wParam = TabEvent, lParam = index
#define WM_PCS_SELFTEST         (0x8000 + 30)
