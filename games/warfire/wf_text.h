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
 *
 *  The enum and the array below are index-for-index aligned, and the colours
 *  in wf_art.cpp's LABEL_COL table are aligned with them too.  Adding a string
 *  to only one of the three silently shifts everything after it -- the labels
 *  still draw, they just draw the wrong words in the wrong colours.  Regenerate
 *  all three together rather than hand-editing one.
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
    WF_LBL_FORGE,
    WF_LBL_TABPERK,
    WF_LBL_FORGE0,
    WF_LBL_FORGE1,
    WF_LBL_FORGE2,
    WF_LBL_FORGEEFF0,
    WF_LBL_FORGEEFF1,
    WF_LBL_FORGEEFF2,
    WF_LBL_BUY,
    WF_LBL_SOLD,
    WF_LBL_NOMONEY,
    WF_LBL_TABHINT,
    WF_LBL_STAT0,
    WF_LBL_STAT1,
    WF_LBL_STAT2,
    WF_LBL_STAT3,
    WF_LBL_STAT4,
    WF_LBL_STAT5,
    WF_LBL_STAT6,
    WF_LBL_STAT7,
    WF_LBL_DASH,
    WF_LBL_SPIN,
    WF_LBL_ENEMYUP,
    WF_LBL_PCT,
    WF_LBL_ARROW,
WF_LABEL_COUNT
};

static const char *const WF_LABEL[WF_LABEL_COUNT] = {
    "等级",      /* 等级 */
    "经验",        /* 经验 */
    "金币",       /* 金币 */
    "击杀",       /* 击杀 */
    "生命",         /* 生命 */
    "怒气",         /* 怒气 */
    "第",     /* 第 */
    "波",     /* 波 */
    "烽火行",      /* 烽火行 */
    "阵亡",       /* 阵亡 */
    "按 R 重来",    /* 按 R 重来 */
    "剩余",       /* 剩余 */
    "开始游戏",      /* 开始游戏 */
    "返回",       /* 返回 */
    "操作说明",      /* 操作说明 */
    "选择强化",    /* 选择强化 */
    "时长",       /* 时长 */
    "最高纪录",       /* 最高纪录 */
    "过关斩将！",        /* 过关斩将！ */
    "目标：坚守到第 10 波并击败首领",    /* 目标：坚守到第 10 波并击败首领 */
    "移动：W A S D / 方向键",       /* 移动：W A S D / 方向键 */
    "攻击：J / 空格 / 鼠标左键",       /* 攻击：J / 空格 / 鼠标左键 */
    "冲刺：K / Shift / 鼠标右键",       /* 冲刺：K / Shift / 鼠标右键 */
    "旋风斩：L / Ctrl（消耗怒气）",       /* 旋风斩：L / Ctrl（消耗怒气） */
    "地图：M 放大查看（开图暂停）",       /* 地图：M 放大查看（开图暂停） */
    "重来：R        退出：Esc",       /* 重来：R        退出：Esc */
    "猛攻",      /* 猛攻 */
    "铁骨",      /* 铁骨 */
    "风靴",      /* 风靴 */
    "迅击",      /* 迅击 */
    "致命",      /* 致命 */
    "嗜血",      /* 嗜血 */
    "飘逸",      /* 飘逸 */
    "旋风",      /* 旋风 */
    "攻击 +5",   /* 攻击 +5 */
    "生命上限 +26",   /* 生命上限 +26 */
    "移动速度提升",   /* 移动速度提升 */
    "攻击速度提升",   /* 攻击速度提升 */
    "暴击率 +6%",   /* 暴击率 +6% */
    "命中回复生命",   /* 命中回复生命 */
    "冲刺冷却缩短",   /* 冲刺冷却缩短 */
    "旋风斩强化",   /* 旋风斩强化 */
    "按 Enter 确认",    /* 按 Enter 确认 */
    "上下键 / 1 2 切换",     /* 上下键 / 1 2 切换 */
    "铁匠铺",      /* 铁匠铺 */
    "强化",    /* 强化 */
    "淬火",     /* 淬火 */
    "护甲",     /* 护甲 */
    "药水",     /* 药水 */
    "攻击 +4",  /* 攻击 +4 */
    "受到伤害 -8%",  /* 受到伤害 -8% */
    "立刻回复 60 生命",  /* 立刻回复 60 生命 */
    "购买",        /* 购买 */
    "已购",       /* 已购 */
    "金币不足",    /* 金币不足 */
    "Tab / Q E 切换页签",    /* Tab / Q E 切换页签 */
    "攻击",      /* 攻击 */
    "生命上限",      /* 生命上限 */
    "移动速度",      /* 移动速度 */
    "攻击速度",      /* 攻击速度 */
    "暴击率",      /* 暴击率 */
    "吸血",      /* 吸血 */
    "冲刺冷却",      /* 冲刺冷却 */
    "旋风冷却",      /* 旋风冷却 */
    "冲刺",       /* 冲刺 */
    "旋风",       /* 旋风 */
    "敌军",    /* 敌军 */
    "%",        /* % */
    "->",      /* -> */
};

#endif /* WF_TEXT_H */
