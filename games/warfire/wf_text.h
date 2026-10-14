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
        WF_LBL_START,
    WF_LBL_BACK,
    WF_LBL_HOWTO,
    WF_LBL_UPGRADE,
    WF_LBL_TIME,
    WF_LBL_BEST,
    WF_LBL_WIN,
    WF_LBL_GOALTIP,
    WF_LBL_HOW0,
    WF_LBL_HOW1,
    WF_LBL_HOW2,
    WF_LBL_HOW3,
    WF_LBL_HOW4,
    WF_LBL_HOW5,
    WF_LBL_PERK0,
    WF_LBL_PERK1,
    WF_LBL_PERK2,
    WF_LBL_PERK3,
    WF_LBL_PERK4,
    WF_LBL_PERK5,
    WF_LBL_PERK6,
    WF_LBL_PERK7,
    WF_LBL_PERKEFF0,
    WF_LBL_PERKEFF1,
    WF_LBL_PERKEFF2,
    WF_LBL_PERKEFF3,
    WF_LBL_PERKEFF4,
    WF_LBL_PERKEFF5,
    WF_LBL_PERKEFF6,
    WF_LBL_PERKEFF7,
    WF_LBL_CONFIRM,
    WF_LBL_SELECT,
WF_LABEL_COUNT
};

static const char *const WF_LABEL[WF_LABEL_COUNT] = {
    "等级",   /* 等级 */
    "经验",   /* 经验 */
    "金币",   /* 金币 */
    "击杀",   /* 击杀 */
    "生命",   /* 生命 */
    "怒气",   /* 怒气 */
    "第",   /* 第   */
    "波",   /* 波   */
    "烽火行",   /* 烽火行 */
    "阵亡",   /* 阵亡 */
    "按 R 重来",   /* 按 R 重来 */
    "剩余",   /* 剩余 */
    "开始游戏",
    "返回",
    "操作说明",
    "选择强化",
    "时长",
    "最高纪录",
    "过关斩将！",
    "目标：坚守到第 10 波并击败首领",
    "移动：W A S D / 方向键",
    "攻击：J / 空格 / 鼠标左键",
    "冲刺：K / Shift / 鼠标右键",
    "旋风斩：L / Ctrl（消耗怒气）",
    "地图：M 放大查看（开图暂停）",
    "重来：R        退出：Esc",
    "猛攻",
    "铁骨",
    "风靴",
    "迅击",
    "致命",
    "嗜血",
    "飘逸",
    "旋风",
    "攻击 +5",
    "生命上限 +26",
    "移动速度提升",
    "攻击速度提升",
    "暴击率 +6%",
    "命中回复生命",
    "冲刺冷却缩短",
    "旋风斩强化",
    "按 Enter 确认",
    "上下键 / 1 2 切换",
};

#endif /* WF_TEXT_H */
