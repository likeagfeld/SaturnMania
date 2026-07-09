#!/usr/bin/env python3
# =============================================================================
# qa_p6_edge_audit.py -- THE BULLETPROOF "did I miss a behavior" GATE.
#
# WHY THIS EXISTS (2026-06-18): the #258 ring port shipped with the hurt->lose-
# rings->scatter path silently broken. FIXED rings rendered + collected (verified),
# but the verbatim PACK-side Player calls Ring_LoseRings, which bound to the PACK
# STUB (p6_closure_edge.c) instead of the OVERLAY's real impl -- so lost rings
# never scattered. The user caught it, not the gates. ROOT METHODOLOGY FAILURE:
# I asserted gates on the ONE path my capture walked (autorun-collect), never the
# hurt path, and never read p6_w_edge_calls (which was 12548 -- the boundary
# detector screaming the whole time).
#
# THE NET: every p6_closure_edge.c stub does P6_EDGE(n) -> ++p6_w_edge_hits[n].
# A BROAD-gameplay capture (collect AND hurt/lose AND, later, die/badnik) makes
# every reachable shadowed function fire. This gate reads the per-ordinal
# histogram and maps each nonzero ordinal -> its decomp function -> owner object,
# and HARD-FAILS if any PORTED object's stub fired (a ported object that hits its
# own stub = a silently-broken behavior, exactly the ring-loss class).
#
# Non-ported stubs that fire are reported as INFO (a TODO list of behaviors whose
# owning object hasn't been ported yet -- expected, not a regression).
#
# BINDING RULE (the methodology fix): EVERY object-port verification MUST run this
# over a capture that exercises the object's FULL behavior set (not just the happy
# path), and assert zero hits for that object's ordinals. "Did I miss a behavior"
# is now a measured RED/GREEN, not the user's eyes.
#
# Usage: python qa_p6_edge_audit.py <capture.mcs> [game.map]
# =============================================================================
import os
import sys
import pathlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as _scene  # noqa: E402

EDGE_MAX = 96

# ordinal -> (decomp function, owner object) -- extracted from p6_closure_edge.c
# P6_EDGE(n) sites. Keep in sync when stubs are added/renumbered.
ORD = {
    1: ("ChaosEmerald_State_Rotate", "ChaosEmerald"), 2: ("CompetitionSession_DeriveWinner", "CompetitionSession"),
    3: ("CompetitionSession_DeriveWinner", "CompetitionSession"), 4: ("CompetitionSession_ResetOptions", "CompetitionSession"),
    5: ("CutsceneRules_DrawCutsceneBounds", "CutsceneRules"), 6: ("CutsceneRules_SetupEntity", "CutsceneRules"),
    7: ("CutsceneSeq_LockAllPlayerControl", "CutsceneSeq"), 8: ("CutsceneSeq_StartSequence", "CutsceneSeq"),
    9: ("Debris_State_Move", "Debris"), 10: ("FXRuby_State_Shrinking", "FXRuby"),
    11: ("ItemBox_Break", "ItemBox"), 12: ("ItemBox_State_Broken", "ItemBox"),
    13: ("ItemBox_State_Falling", "ItemBox"), 14: ("ItemBox_State_Idle", "ItemBox"),
    15: ("KleptoMobile_StateArm_Cutscene", "KleptoMobile"), 16: ("KleptoMobile_StateArm_Idle", "KleptoMobile"),
    17: ("KleptoMobile_StateHand_Boss", "KleptoMobile"), 18: ("KleptoMobile_StateHand_Cutscene", "KleptoMobile"),
    19: ("KleptoMobile_State_CutsceneControlled", "KleptoMobile"), 20: ("KleptoMobile_State_CutsceneControlled", "KleptoMobile"),
    21: ("PhantomKing_SetupKing", "PhantomKing"), 22: ("PhantomKing_StateArm_Idle", "PhantomKing"),
    23: ("PhantomKing_StateArm_WrestleEggman", "PhantomKing"), 24: ("PhantomKing_State_SetupArms", "PhantomKing"),
    25: ("PhantomKing_State_TakeRubyAway", "PhantomKing"), 26: ("PhantomKing_State_WrestleEggman", "PhantomKing"),
    27: ("PhantomRuby_PlaySfx", "PhantomRuby"),
    28: ("Ring_Draw_Normal", "Ring"), 29: ("Ring_LoseHyperRings", "Ring"),
    30: ("Ring_LoseRings", "Ring"), 31: ("Ring_State_Lost", "Ring"),
    32: ("UIButtonPrompt_GetGamepadType", "UIButtonPrompt"), 33: ("UIButtonPrompt_MappingsToFrame", "UIButtonPrompt"),
    34: ("UIControl_SetMenuLostFocus", "UIControl"), 35: ("UIDialog_AddButton", "UIDialog"),
    36: ("UIDialog_CloseOnSel_HandleSelection", "UIDialog"), 37: ("UIDialog_CloseOnSel_HandleSelection", "UIDialog"),
    38: ("UIDialog_CloseOnSel_HandleSelection", "UIDialog"), 39: ("UIDialog_Setup", "UIDialog"),
    40: ("UIWaitSpinner_FinishWait", "UIWaitSpinner"), 41: ("UIWaitSpinner_StartWait", "UIWaitSpinner"),
    42: ("UIWidgets_DrawParallelogram", "UIWidgets"), 43: ("UIWidgets_DrawRightTriangle", "UIWidgets"),
    44: ("APICallback_AssignControllerID", "APICallback"), 45: ("APICallback_ControllerIDForInputID", "APICallback"),
    46: ("APICallback_GetConfirmButtonFlip", "APICallback"), 47: ("APICallback_GetUserAuthStatus", "APICallback"),
    48: ("APICallback_InputIDIsDisconnected", "APICallback"), 49: ("APICallback_MostRecentActiveControllerID", "APICallback"),
    50: ("APICallback_SaveUserFile", "APICallback"), 51: ("APICallback_SetRichPresence", "APICallback"),
    52: ("APICallback_SetRichPresence", "APICallback"), 53: ("TimeAttackData_AddRecord", "TimeAttackData"),
    54: ("TimeAttackData_GetPackedTime", "TimeAttackData"), 55: ("StarPost_ResetStarPosts", "StarPost"),
    56: ("GameProgress_MarkZoneCompleted", "GameProgress"), 57: ("APICallback_TrackTAClear", "APICallback"),
    58: ("APICallback_TrackActClear", "APICallback"), 59: ("Ring_State_Sparkle", "Ring"),
    60: ("Ring_Draw_Sparkle", "Ring"), 61: ("Announcer_AnnounceGoal", "Announcer"),
    62: ("APICallback_UnlockAchievement", "APICallback"), 63: ("Platform_State_Falling2", "Platform"),
    64: ("Platform_State_Hold", "Platform"),
    # Batch 2 (badnik break chain): Player_CheckBadnikBreak's non-PLUS achievement-
    # tracking call. APICallback is backlog plumbing (NULL'd, not a registered object),
    # so a hit here is non-gameplay (a no-op on Saturn), NOT a shadowed ported object.
    65: ("APICallback_TrackEnemyDefeat", "APICallback"),
    # Batch 2 forward (badnik-break): fires only if the overlay forward is UNWIRED.
    66: ("BadnikHelpers_BadnikBreakUnseeded/Break", "BadnikHelpers"),
    # Batch 3 (2026-07-09 GHZ gameplay-parity sweep): 68 = the LRZ conveyor helper
    # (LRZ-only; GHZ1-dead by NULL-guard). 69 = the Platform_State_Fall pack stub
    # (referenced by the overlay ItemBox; dead once Game_Platform.o joins the
    # overlay -- the intra-overlay definition wins over the -R import).
    68: ("LRZConvItem_HandleLRZConvPhys", "LRZConvItem"),
    69: ("Platform_State_Fall", "Platform"),
}

# Objects whose REAL code is ported (overlay or pack). A stub hit for ANY of these
# is a HARD failure -- the ported object's behavior is silently shadowed.
# Batch 3: + ItemBox/Debris. Their pack stubs FORWARD to the overlay impls and
# return BEFORE P6_EDGE when wired -- so a nonzero hit means the forward is
# unwired == the ported behavior is shadowed (the #258 class). InvincibleStars
# has no stubs (fully overlay-internal).
PORTED = {"Ring", "Spring", "Bridge", "PlaneSwitch", "SpikeLog", "Spikes",
          "ItemBox", "Debris"}


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    mcs = _scene._as_path(args[0]) if args else os.path.join(HERE, "p6_edge.mcs")
    mp = _scene._as_path(args[1]) if len(args) > 1 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("CLOSURE-EDGE AUDIT (#258b): map EVERY shadowed boundary fn that fired")
    print("=" * 72)
    if not (os.path.isfile(mcs) and os.path.isfile(mp)):
        print("RESULT: RED -- capture or map missing (%s)" % mcs)
        return 1
    map_text = _scene.read_text(mp)
    base = _scene.map_symbol(map_text, "_p6_w_edge_hits")
    callsym = _scene.map_symbol(map_text, "_p6_w_edge_calls")
    if base is None:
        print("RESULT: RED -- p6_w_edge_hits[] not in map (histogram not built in).")
        return 1
    mod = _scene.load_harness()
    sec = mod.parse_savestate(pathlib.Path(mcs))
    _, perm = _scene.calibrate(mod._peek_bytes(sec, _scene.map_symbol(map_text, _scene.SYM_MAGIC), 4))
    if perm is None:
        print("RESULT: RED -- magic mis-decode")
        return 1

    total = _scene.peek_u32(mod, sec, callsym, perm, signed=True) if callsym else -1
    hits = {}
    for n in range(EDGE_MAX):
        v = _scene.peek_u32(mod, sec, base + n * 4, perm, signed=True)
        if v:
            hits[n] = v

    print("  p6_w_edge_calls (total boundary crossings) = %d" % total)
    print("  distinct stubs that fired = %d" % len(hits))
    print("-" * 72)

    ported_hits, unported_hits = [], []
    for n in sorted(hits):
        fn, owner = ORD.get(n, ("<ordinal %d unmapped>" % n, "?"))
        (ported_hits if owner in PORTED else unported_hits).append((n, hits[n], fn, owner))

    if ported_hits:
        print("  [ RED ] PORTED-object stubs fired (silently-broken behavior):")
        for n, c, fn, owner in ported_hits:
            print("          ord %2d  %-26s (%s)  hits=%d" % (n, fn, owner, c))
    else:
        print("  [GREEN] no PORTED-object stub fired (no shadowed ported behavior)")
    if unported_hits:
        print("  [INFO ] unported-object stubs fired (TODO: port these objects):")
        for n, c, fn, owner in unported_hits:
            print("          ord %2d  %-26s (%s)  hits=%d" % (n, fn, owner, c))
    print("-" * 72)
    if ported_hits:
        print("RESULT: RED -- a PORTED object is hitting its own stub (behavior lost).")
        return 1
    print("RESULT: GREEN -- every ported object's behavior reaches its real code.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
