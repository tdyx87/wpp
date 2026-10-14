#ifndef WF_TEXT_H
#define WF_TEXT_H

/* ---------------------------------------------------------------------------
 *  wf_text.h -- Warfire : Chinese UI strings
 *
 *  *** THIS FILE IS ENCODED IN GBK (code page 936). ***
 *
 *  Everything else under games/warfire is pure ASCII; this file is the single
 *  exception.  WFont renders GB2312: the compiler reads the bytes using the
 *  system ANSI code page and WFont looks each character up as a two-byte GBK
 *  code.  Do not re-save it as UTF-8.
 * ---------------------------------------------------------------------------
 */

enum {
    WF_LBL_LEVEL = 0,
    WF_LBL_EXP,
    WF_LBL_GOLD,
    WF_LBL_KILL,
    WF_LBL_HP,
    WF_LBL_MP,
    WF_LBL_WAVE_A,
    WF_LBL_WAVE_B,
    WF_LBL_TITLE,
    WF_LBL_DEAD,
    WF_LBL_RESTART,
    WF_LBL_LEFT,
    WF_LABEL_COUNT
};

static const char *const WF_LABEL[WF_LABEL_COUNT] = {
    "等级",                   /* 等级 */
    "经验",                   /* 经验 */
    "金币",                   /* 金币 */
    "击杀",                   /* 击杀 */
    "生命",                   /* 生命 */
    "怒气",                   /* 怒气 */
    "第",                         /* 第   */
    "波",                         /* 波   */
    "烽火行",             /* 烽火行 */
    "阵亡",                   /* 阵亡 */
    "按 R 重来",          /* 按 R 重来 */
    "剩余"                    /* 剩余 */
};

#endif /* WF_TEXT_H */
