# The remembered effect slots of Aquas' Garoa and Area 6's Gorgon (#262): one line each time one of them uses the slot
# it remembers, with what is in that slot. For `python tools/coop/w7asan_sweep.py sweep --levels aquas,area-6 --gdb
# tools/coop/w7asan_slots.gdb`, which counts the lines whose ok is 0: the slot is no longer the beam it was.
# The line numbers are those of this commit; a break that lands elsewhere after an edit prints nonsense, so check
# them against the comments (`sed -n <line>p <file>`).
set pagination off
set confirm off
set print thread-events off
set breakpoint pending on
handle SIGPIPE nostop noprint pass

# Aquas_AqGaroa_Update, a dead Garoa looks at the beams it still holds: `if ((effect->obj.id == OBJ_EFFECT_395) && ...`
break fox_aq.c:4698
commands
silent
printf "w7trace garoa-dead ok=%d frame=%d actor=%d slot=%d status=%d id=%d state=%d unk_44=%d timer_50=%d\n", (effect->obj.status == OBJ_ACTIVE) && (effect->obj.id == OBJ_EFFECT_395) && (effect->state == 2), gGameFrameCount, this->index, j, effect->obj.status, effect->obj.id, effect->state, effect->unk_44, effect->timer_50
continue
end

# state 3, after both beams were asked for: `this->timer_0C0 = 10;`. A beam that got no slot comes back as slot 0.
break fox_aq.c:4798
commands
silent
printf "w7trace garoa-take ok=%d frame=%d actor=%d slots=%d,%d id=%d,%d state=%d,%d\n", (gEffects[this->iwork[2] - 1].obj.id == OBJ_EFFECT_395) && (gEffects[this->iwork[2] - 1].state == 2) && (gEffects[this->iwork[3] - 1].obj.id == OBJ_EFFECT_395) && (gEffects[this->iwork[3] - 1].state == 2) && (this->iwork[2] != this->iwork[3]), gGameFrameCount, this->index, (int) this->iwork[2] - 1, (int) this->iwork[3] - 1, gEffects[this->iwork[2] - 1].obj.id, gEffects[this->iwork[3] - 1].obj.id, gEffects[this->iwork[2] - 1].state, gEffects[this->iwork[3] - 1].state
continue
end

# state 4, the second beam (iwork[3]) held or thrown: `if (this->animFrame == 20) {`
break fox_aq.c:4820
commands
silent
printf "w7trace garoa-hold ok=%d frame=%d actor=%d beam=3 anim=%d slot=%d status=%d id=%d state=%d unk_44=%d timer_50=%d\n", (effect->obj.status == OBJ_ACTIVE) && (effect->obj.id == OBJ_EFFECT_395) && (effect->state == 2), gGameFrameCount, this->index, this->animFrame, j, effect->obj.status, effect->obj.id, effect->state, effect->unk_44, effect->timer_50
continue
end

# state 4, the first beam (iwork[2]): `if (this->animFrame == 57) {`
break fox_aq.c:4854
commands
silent
printf "w7trace garoa-hold ok=%d frame=%d actor=%d beam=2 anim=%d slot=%d status=%d id=%d state=%d unk_44=%d timer_50=%d\n", (effect->obj.status == OBJ_ACTIVE) && (effect->obj.id == OBJ_EFFECT_395) && (effect->state == 2), gGameFrameCount, this->index, this->animFrame, j, effect->obj.status, effect->obj.id, effect->state, effect->unk_44, effect->timer_50
continue
end

# Area6_A6Gorgon_Update, state 7, the beam in swork[A6_SWK_39]: `Math_SmoothStepToF(&D_i3_801C22F0.unk_28[0], 255.0f, ...`
break fox_a6.c:1080
commands
silent
printf "w7trace gorgon-beam ok=%d frame=%d slot=%d status=%d id=%d state=%d unk_44=%d unk_60.z=%f\n", (effect->obj.status == OBJ_ACTIVE) && (effect->obj.id == OBJ_EFFECT_395) && (effect->state == 10), gGameFrameCount, this->swork[39], effect->obj.status, effect->obj.id, effect->state, effect->unk_44, effect->unk_60.z
continue
end

run
