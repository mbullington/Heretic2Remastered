//
// cl_input.c -- builds an intended movement command to send to the server
//
// Copyright 1998 Raven Software
//

#include "client.h"
#include "EffectFlags.h"
#include "p_anims.h"
#include "Vector.h"

static cvar_t* cl_nodelta;
static cvar_t* cl_inputwatch;
static cvar_t* cl_protocol51_unstick;
static cvar_t* cl_protocol51_strip_any;
static cvar_t* cl_protocol51_nodelta;

static uint frame_msec;
static uint old_sys_frame_time;
static float quickturn_time; // H2
static int inputwatch_next_time;
static int protocol51_unstick_origin_time;
static int protocol51_unstick_recovery_until;
static int protocol51_unstick_message_time;
static short protocol51_unstick_origin[3];

int pred_pm_flags; // H2
int pred_pm_w_flags; // H2

qboolean command_down; // H2

// KEY BUTTONS

// Continuous button event tracking is complicated by the fact that two different input sources
// (say, mouse button 1 and the control key) can both press the same button,
// but the button should only be released when both of the pressing key have been released.

// When a key event issues a button command (+forward, +attack, etc), it appends its key number
// as a parameter to the command so it can be matched up with the release.

// state bit 0 is the current state of the key
// state bit 1 is edge triggered on the up to down transition
// state bit 2 is edge triggered on the down to up transition

#define MOVE_SCALER	64.0f //mxd

qboolean in_do_autoaim;

kbutton_t in_klook;
kbutton_t in_strafe;
kbutton_t in_speed;
kbutton_t in_lookaround;

static kbutton_t in_left;
static kbutton_t in_right;
static kbutton_t in_forward;
static kbutton_t in_back;

static kbutton_t in_lookup;
static kbutton_t in_lookdown;
static kbutton_t in_moveleft;
static kbutton_t in_moveright;

static kbutton_t in_up;
kbutton_t in_down;

static kbutton_t in_attack;
static kbutton_t in_defend; // H2
static kbutton_t in_action; // H2

static kbutton_t in_creep; // H2
static kbutton_t in_inventory; // H2
static kbutton_t in_quickturn; // H2
static kbutton_t in_autoaim; // H2

// Q2 counterpart
static void KeyUp(kbutton_t* b)
{
	const char* c = Cmd_Argv(1);

	if (c[0] == 0)
	{
		// Typed manually at the console, assume for unsticking, so clear all.
		b->down[0] = 0;
		b->down[1] = 0;
		b->state = KS_IMPULSE_UP; // Impulse up.

		return;
	}

	const int k = Q_atoi(c);

	if (b->down[0] == k)
		b->down[0] = 0;
	else if (b->down[1] == k)
		b->down[1] = 0;
	else
		return; // Key up without corresponding down (menu pass through).

	if (b->down[0] || b->down[1] || !(b->state & KS_DOWN))
		return; // Some other key is still holding it down or still up (this should not happen).

	// Save timestamp.
	const uint uptime = Q_atoi(Cmd_Argv(2));
	if (uptime > 0)
		b->msec += uptime - b->downtime;
	else
		b->msec += 10;

	b->state &= ~KS_DOWN; // Now up.
	b->state |= KS_IMPULSE_UP; // Impulse up.
}

// Q2 counterpart
static void KeyDown(kbutton_t* b)
{
	int k;

	const char* c = Cmd_Argv(1);
	if (c[0])
		k = Q_atoi(c);
	else
		k = -1; // Typed manually at the console for continuous down.

	if (k == b->down[0] || k == b->down[1])
		return; // Repeating key.

	if (!b->down[0])
	{
		b->down[0] = k;
	}
	else if (!b->down[1])
	{
		b->down[1] = k;
	}
	else
	{
		Com_Printf("Three keys down for a button!\n");
		return;
	}

	if (b->state & KS_DOWN)
		return; // Still down.

	// Save timestamp.
	c = Cmd_Argv(2);
	b->downtime = Q_atoi(c);

	if (b->downtime == 0)
		b->downtime = sys_frame_time - 100;

	b->state |= (KS_DOWN | KS_IMPULSE_DOWN); // Down + impulse down.
}

// Q2 counterpart
static void IN_UpDown(void)
{
	KeyDown(&in_up);
}

// Q2 counterpart
static void IN_UpUp(void)
{
	KeyUp(&in_up);
}

// Q2 counterpart
static void IN_DownDown(void)
{
	KeyDown(&in_down);
}

// Q2 counterpart
static void IN_DownUp(void)
{
	KeyUp(&in_down);
}

// Q2 counterpart
static void IN_LeftDown(void)
{
	KeyDown(&in_left);
}

// Q2 counterpart
static void IN_LeftUp(void)
{
	KeyUp(&in_left);
}

// Q2 counterpart
static void IN_RightDown(void)
{
	KeyDown(&in_right);
}

// Q2 counterpart
static void IN_RightUp(void)
{
	KeyUp(&in_right);
}

// Q2 counterpart
static void IN_ForwardDown(void)
{
	KeyDown(&in_forward);
}

// Q2 counterpart
static void IN_ForwardUp(void)
{
	KeyUp(&in_forward);
}

// Q2 counterpart
static void IN_BackDown(void)
{
	KeyDown(&in_back);
}

// Q2 counterpart
static void IN_BackUp(void)
{
	KeyUp(&in_back);
}

// Q2 counterpart
static void IN_LookupDown(void)
{
	KeyDown(&in_lookup);
}

// Q2 counterpart
static void IN_LookupUp(void)
{
	KeyUp(&in_lookup);
}

// Q2 counterpart
static void IN_LookdownDown(void)
{
	KeyDown(&in_lookdown);
}

// Q2 counterpart
static void IN_LookdownUp(void)
{
	KeyUp(&in_lookdown);
}

// Q2 counterpart
static void IN_StrafeDown(void)
{
	KeyDown(&in_strafe);
}

// Q2 counterpart
static void IN_StrafeUp(void)
{
	KeyUp(&in_strafe);
}

// Q2 counterpart
static void IN_MoveleftDown(void)
{
	KeyDown(&in_moveleft);
}

// Q2 counterpart
static void IN_MoveleftUp(void)
{
	KeyUp(&in_moveleft);
}

// Q2 counterpart
static void IN_MoverightDown(void)
{
	KeyDown(&in_moveright);
}

// Q2 counterpart
static void IN_MoverightUp(void)
{
	KeyUp(&in_moveright);
}

// Q2 counterpart
static void IN_AttackDown(void)
{
	KeyDown(&in_attack);
}

// Q2 counterpart
static void IN_AttackUp(void)
{
	KeyUp(&in_attack);
}

static void IN_DefendDown(void) // H2
{
	KeyDown(&in_defend);
}

static void IN_DefendUp(void) // H2
{
	KeyUp(&in_defend);
}

static void IN_ActionDown(void) // H2
{
	KeyDown(&in_action);
}

static void IN_ActionUp(void) // H2
{
	KeyUp(&in_action);
}

static void IN_CreepDown(void) // H2
{
	KeyDown(&in_creep);
}

static void IN_CreepUp(void) // H2
{
	KeyUp(&in_creep);
}

static void IN_AutoaimDown(void) // H2
{
	KeyDown(&in_autoaim);
}

static void IN_AutoaimUp(void) // H2
{
	KeyUp(&in_autoaim);
}

static void IN_KLookDown(void)
{
	KeyDown(&in_klook);
}

static void IN_KLookUp(void)
{
	if (!(int)freelook->value && (int)lookspring->value) // H2
		IN_CenterView();

	KeyUp(&in_klook);
}

static void IN_SpeedDown(void) // H2
{
	KeyDown(&in_speed);
}

static void IN_SpeedUp(void) // H2
{
	KeyUp(&in_speed);
}

static void IN_LookaroundDown(void) // H2
{
	KeyDown(&in_lookaround);
}

static void IN_LookaroundUp(void) // H2
{
	KeyUp(&in_lookaround);
}

static void IN_CommandDown(void) // H2
{
	command_down = true;
}

static void IN_CommandUp(void) // H2
{
	command_down = false;
}

static void IN_QuickturnDown(void) // H2
{
	KeyDown(&in_quickturn);
}

static void IN_QuickturnUp(void) // H2
{
	KeyUp(&in_quickturn);
}

static void IN_InventoryDown(void) // H2
{
	KeyDown(&in_inventory);
}

static void IN_InventoryUp(void) // H2
{
	KeyUp(&in_inventory);
}

void IN_CenterView(void)
{
	const float angle = -SHORT2ANGLE(cl.frame.playerstate.pmove.delta_angles[PITCH]);
	cl.inputangles[PITCH] = angle;
	cl.viewangles[PITCH] = angle;
}

static FILE* cl_stuckinfo_file;

static void CL_StuckInfoPrint(const char* fmt, ...)
{
	char msg[1024];
	va_list argptr;
	va_start(argptr, fmt);
	vsprintf_s(msg, sizeof(msg), fmt, argptr);
	va_end(argptr);

	Com_Printf("%s", msg);

	if (cl_stuckinfo_file != NULL)
	{
		fprintf(cl_stuckinfo_file, "%s", msg);
		fflush(cl_stuckinfo_file);
	}
}

static void CL_StuckInfoPrintKey(const char* name, const kbutton_t* key)
{
	CL_StuckInfoPrint("%-12s state=0x%x down0=%i:%s down1=%i:%s downtime=%u msec=%u\n",
		name,
		key->state,
		key->down[0], (key->down[0] > 0 ? Key_KeynumToString(key->down[0]) : "<none>"),
		key->down[1], (key->down[1] > 0 ? Key_KeynumToString(key->down[1]) : "<none>"),
		key->downtime,
		key->msec);
}

static void CL_StuckInfoPrintCmd(const char* label, const usercmd_t* cmd)
{
	CL_StuckInfoPrint("%s msec=%i fwd=%i side=%i up=%i buttons=0x%x%s%s%s%s%s%s%s%s%s angles=(%i %i %i) aim=(%i %i %i) light=%i pred=%i\n",
		label,
		cmd->msec,
		cmd->forwardmove,
		cmd->sidemove,
		cmd->upmove,
		cmd->buttons,
		(cmd->buttons & BUTTON_ATTACK ? " ATTACK" : ""),
		(cmd->buttons & BUTTON_DEFEND ? " DEFEND" : ""),
		(cmd->buttons & BUTTON_ACTION ? " ACTION" : ""),
		(cmd->buttons & BUTTON_RUN ? " RUN" : ""),
		(cmd->buttons & BUTTON_CREEP ? " CREEP" : ""),
		(cmd->buttons & BUTTON_LOOKAROUND ? " LOOK" : ""),
		(cmd->buttons & BUTTON_INVENTORY ? " INV" : ""),
		(cmd->buttons & BUTTON_QUICKTURN ? " QUICKTURN" : ""),
		(cmd->buttons & BUTTON_ANY ? " ANY" : ""),
		cmd->angles[0], cmd->angles[1], cmd->angles[2],
		cmd->aimangles[0], cmd->aimangles[1], cmd->aimangles[2],
		cmd->lightlevel,
		cmd->prediction);
}

static void CL_StuckInfo_f(void)
{
	char name[MAX_OSPATH];
	Com_sprintf(name, sizeof(name), "%s/client_stuckinfo.txt", FS_Userdir());
	fopen_s(&cl_stuckinfo_file, name, "a");
	if (cl_stuckinfo_file != NULL)
		fprintf(cl_stuckinfo_file, "\n");

	CL_StuckInfoPrint("client stuckinfo log: %s\n", name);
	CL_StuckInfoPrint("--- client stuckinfo realtime=%i state=%i key_dest=%i protocol=%i serverProtocol=%i server=%s ---\n",
		cls.realtime,
		cls.state,
		cls.key_dest,
		CL_GetProtocolVersion(),
		cls.serverProtocol,
		cls.servername);
	CL_StuckInfoPrint("ignore_input=%i cinematicfreeze=%i remote_id=%i anykeydown=%i force_packet=%i outgoing=%i incoming=%i dropped=%i\n",
		CL_IgnoreInput(),
		cl.frame.playerstate.cinematicfreeze,
		cl.frame.playerstate.remote_id,
		anykeydown,
		cls.force_packet,
		cls.netchan.outgoing_sequence,
		cls.netchan.incoming_sequence,
		cls.netchan.dropped);
	CL_StuckInfoPrint("viewangles=(%.2f %.2f %.2f) inputangles=(%.2f %.2f %.2f) delta_inputangles=(%.2f %.2f %.2f) predicted_origin=(%.2f %.2f %.2f) prediction_error=(%.2f %.2f %.2f)\n",
		cl.viewangles[0], cl.viewangles[1], cl.viewangles[2],
		cl.inputangles[0], cl.inputangles[1], cl.inputangles[2],
		cl.delta_inputangles[0], cl.delta_inputangles[1], cl.delta_inputangles[2],
		cl.predicted_origin[0], cl.predicted_origin[1], cl.predicted_origin[2],
		cl.prediction_error[0], cl.prediction_error[1], cl.prediction_error[2]);
	CL_StuckInfoPrint("pmove type=%i flags=0x%x w_flags=0x%x time=%i origin=(%i %i %i) velocity=(%i %i %i) pred_pm_flags=0x%x pred_pm_w_flags=0x%x\n",
		cl.frame.playerstate.pmove.pm_type,
		cl.frame.playerstate.pmove.pm_flags,
		cl.frame.playerstate.pmove.w_flags,
		cl.frame.playerstate.pmove.pm_time,
		cl.frame.playerstate.pmove.origin[0], cl.frame.playerstate.pmove.origin[1], cl.frame.playerstate.pmove.origin[2],
		cl.frame.playerstate.pmove.velocity[0], cl.frame.playerstate.pmove.velocity[1], cl.frame.playerstate.pmove.velocity[2],
		pred_pm_flags,
		pred_pm_w_flags);

	CL_StuckInfoPrintKey("attack", &in_attack);
	CL_StuckInfoPrintKey("defend", &in_defend);
	CL_StuckInfoPrintKey("action", &in_action);
	CL_StuckInfoPrintKey("forward", &in_forward);
	CL_StuckInfoPrintKey("back", &in_back);
	CL_StuckInfoPrintKey("moveleft", &in_moveleft);
	CL_StuckInfoPrintKey("moveright", &in_moveright);
	CL_StuckInfoPrintKey("speed", &in_speed);
	CL_StuckInfoPrintKey("creep", &in_creep);
	CL_StuckInfoPrintKey("lookaround", &in_lookaround);
	CL_StuckInfoPrintKey("quickturn", &in_quickturn);
	CL_StuckInfoPrintKey("inventory", &in_inventory);

	const playerinfo_t* info = &cl.playerinfo;
	CL_StuckInfoPrint("playerinfo buttons=0x%x old=0x%x latched=0x%x remember=0x%x seq_attack=%i upper=%i/%i lower=%i/%i weapon=%s ready=%i charge=%i flags=0x%x pm_flags=0x%x pm_w_flags=0x%x\n",
		info->buttons,
		info->oldbuttons,
		info->latched_buttons,
		info->remember_buttons,
		info->seqcmd[ACMDU_ATTACK],
		info->upperseq,
		info->upperframe,
		info->lowerseq,
		info->lowerframe,
		(info->pers.weapon != NULL ? info->pers.weapon->classname : "<none>"),
		info->pers.weaponready,
		info->weaponcharge,
		info->flags,
		info->pm_flags,
		info->pm_w_flags);

	CL_StuckInfoPrintCmd("cl.cmd", &cl.cmd);
	for (int i = 0; i < 4; i++)
	{
		const int sequence = cls.netchan.outgoing_sequence - i;
		CL_StuckInfoPrintCmd(va("cmd[%i]", sequence), &cl.cmds[sequence & (CMD_BACKUP - 1)]);
	}

	CL_StuckInfoPrint("--- end client stuckinfo ---\n");

	if (cl_stuckinfo_file != NULL)
	{
		fclose(cl_stuckinfo_file);
		cl_stuckinfo_file = NULL;
	}
}

static void CL_ClearKButton(kbutton_t* key)
{
	memset(key, 0, sizeof(*key));
}

static void CL_InputWatchFilePrintf(FILE* f, const char* fmt, ...)
{
	char msg[1024];
	va_list argptr;
	va_start(argptr, fmt);
	vsprintf_s(msg, sizeof(msg), fmt, argptr);
	va_end(argptr);

	fprintf(f, "%s", msg);
}

static void CL_InputWatchPrintKey(FILE* f, const char* name, const kbutton_t* key)
{
	CL_InputWatchFilePrintf(f, " %s=0x%x/%i:%s/%i:%s",
		name,
		key->state,
		key->down[0], (key->down[0] > 0 ? Key_KeynumToString(key->down[0]) : "-"),
		key->down[1], (key->down[1] > 0 ? Key_KeynumToString(key->down[1]) : "-"));
}

static void CL_InputWatchTrace(FILE* f, const char* label, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, const int flags)
{
	trace_t tr;
	CL_Trace(start, mins, maxs, end, MASK_PLAYERSOLID, flags | CTF_ALWAYS_CHECK_CONTENTS, &tr);
	CL_InputWatchFilePrintf(f, "  trace %-9s start=%i all=%i frac=%.3f end=(%.1f %.1f %.1f) contents=0x%x ent=%p\n",
		label,
		tr.startsolid,
		tr.allsolid,
		tr.fraction,
		tr.endpos[0], tr.endpos[1], tr.endpos[2],
		tr.contents,
		tr.ent);
}

static void CL_InputWatchCollision(FILE* f, const usercmd_t* cmd)
{
	const vec3_t origin = VEC3_INITS(cl.frame.playerstate.pmove.origin, 0.125f);
	vec3_t mins = VEC3_INIT(cl.playerinfo.mins);
	vec3_t maxs = VEC3_INIT(cl.playerinfo.maxs);

	if (Vec3IsZero(mins) && Vec3IsZero(maxs))
	{
		VectorSet(mins, -16.0f, -16.0f, -24.0f);
		VectorSet(maxs, 16.0f, 16.0f, 48.0f);
	}

	vec3_t forward;
	vec3_t right;
	vec3_t up;
	vec3_t angles = VEC3_SET(0.0f, SHORT2ANGLE(cl.frame.playerstate.pmove.delta_angles[YAW] + cmd->angles[YAW]), 0.0f);
	AngleVectors(angles, forward, right, up);

	vec3_t end;
	const vec3_t feet = VEC3_INITA(origin, 0.0f, 0.0f, mins[2] + 1.0f);
	const vec3_t head = VEC3_INITA(origin, 0.0f, 0.0f, maxs[2]);
	CL_InputWatchFilePrintf(f, "  bbox mins=(%.1f %.1f %.1f) maxs=(%.1f %.1f %.1f) pointcontents origin=0x%x feet=0x%x head=0x%x\n",
		mins[0], mins[1], mins[2],
		maxs[0], maxs[1], maxs[2],
		CL_PMpointcontents(origin),
		CL_PMpointcontents(feet),
		CL_PMpointcontents(head));

	CL_InputWatchTrace(f, "zero/world", origin, mins, maxs, origin, CTF_CLIP_TO_WORLD);
	CL_InputWatchTrace(f, "zero/all", origin, mins, maxs, origin, CTF_CLIP_TO_ALL | CTF_IGNORE_PLAYER);

	VectorMA(origin, 32.0f, forward, end);
	CL_InputWatchTrace(f, "fwd/world", origin, mins, maxs, end, CTF_CLIP_TO_WORLD);
	CL_InputWatchTrace(f, "fwd/all", origin, mins, maxs, end, CTF_CLIP_TO_ALL | CTF_IGNORE_PLAYER);

	VectorMA(origin, -32.0f, forward, end);
	CL_InputWatchTrace(f, "back/world", origin, mins, maxs, end, CTF_CLIP_TO_WORLD);
	VectorMA(origin, 32.0f, right, end);
	CL_InputWatchTrace(f, "right/world", origin, mins, maxs, end, CTF_CLIP_TO_WORLD);
	VectorMA(origin, -32.0f, right, end);
	CL_InputWatchTrace(f, "left/world", origin, mins, maxs, end, CTF_CLIP_TO_WORLD);
	VectorMA(origin, -32.0f, up, end);
	CL_InputWatchTrace(f, "down/world", origin, mins, maxs, end, CTF_CLIP_TO_WORLD);
}

static void CL_InputWatchLog(const usercmd_t* cmd)
{
	char name[MAX_OSPATH];
	Com_sprintf(name, sizeof(name), "%s/client_inputwatch.txt", FS_Userdir());

	FILE* f;
	if (fopen_s(&f, name, "a") != 0 || f == NULL)
		return;

	const player_state_t* ps = &cl.frame.playerstate;
	CL_InputWatchFilePrintf(f, "watch realtime=%i key_dest=%i anykeydown=%i ignore=%i seq=%i/%i drop=%i serverProtocol=%i predict=%i p51_predict=%g p51_unstick=%i strip_any=%g nodelta=%g\n",
		cls.realtime,
		cls.key_dest,
		anykeydown,
		CL_IgnoreInput(),
		cls.netchan.outgoing_sequence,
		cls.netchan.incoming_sequence,
		cls.netchan.dropped,
		cls.serverProtocol,
		CL_PREDICT,
		cl_protocol51_predict->value,
		(cls.realtime < protocol51_unstick_recovery_until),
		cl_protocol51_strip_any->value,
		(cls.serverProtocol == PROTOCOL_VERSION && (int)cl_protocol51_nodelta->value ? 1.0f : cl_nodelta->value));
	CL_InputWatchFilePrintf(f, "  frame server=%i delta=%i time=%i cltime=%i valid=%i frame_parse=%i frame_ents=%i parse_head=%i playernum=%i\n",
		cl.frame.serverframe,
		cl.frame.deltaframe,
		cl.frame.servertime,
		cl.time,
		cl.frame.valid,
		cl.frame.parse_entities,
		cl.frame.num_entities,
		cl.parse_entities,
		cl.playernum);
	CL_InputWatchFilePrintf(f, "  cmd msec=%i fwd=%i side=%i up=%i buttons=0x%x%s%s%s%s%s angles=(%i %i %i) aim=(%i %i %i)\n",
		cmd->msec,
		cmd->forwardmove,
		cmd->sidemove,
		cmd->upmove,
		cmd->buttons,
		(cmd->buttons & BUTTON_ATTACK ? " ATTACK" : ""),
		(cmd->buttons & BUTTON_ACTION ? " ACTION" : ""),
		(cmd->buttons & BUTTON_RUN ? " RUN" : ""),
		(cmd->buttons & BUTTON_AUTOAIM ? " AUTOAIM" : ""),
		(cmd->buttons & BUTTON_ANY ? " ANY" : ""),
		cmd->angles[0], cmd->angles[1], cmd->angles[2],
		cmd->aimangles[0], cmd->aimangles[1], cmd->aimangles[2]);
	CL_InputWatchFilePrintf(f, "  pmove type=%i flags=0x%x w=0x%x c=0x%x time=%i gravity=%i origin=(%i %i %i) velocity=(%i %i %i) delta=(%i %i %i) camdelta=(%i %i %i) predicted=(%.1f %.1f %.1f)\n",
		ps->pmove.pm_type,
		ps->pmove.pm_flags,
		ps->pmove.w_flags,
		ps->pmove.c_flags,
		ps->pmove.pm_time,
		ps->pmove.gravity,
		ps->pmove.origin[0], ps->pmove.origin[1], ps->pmove.origin[2],
		ps->pmove.velocity[0], ps->pmove.velocity[1], ps->pmove.velocity[2],
		ps->pmove.delta_angles[0], ps->pmove.delta_angles[1], ps->pmove.delta_angles[2],
		ps->pmove.camera_delta_angles[0], ps->pmove.camera_delta_angles[1], ps->pmove.camera_delta_angles[2],
		cl.predicted_origin[0], cl.predicted_origin[1], cl.predicted_origin[2]);
	CL_InputWatchFilePrintf(f, "  ps viewheight=%i fov=%.1f rdflags=0x%x remote_id=%i freeze=%i dead=%i flags=0x%x edictflags=0x%x fwdvel=%.1f sidevel=%.1f upvel=%.1f upper=%i/%i lower=%i/%i weapon=%i ready=%i mins=(%.1f %.1f %.1f) maxs=(%.1f %.1f %.1f)\n",
		ps->viewheight,
		ps->fov,
		ps->rdflags,
		ps->remote_id,
		ps->cinematicfreeze,
		ps->deadflag,
		ps->flags,
		ps->edictflags,
		ps->fwdvel,
		ps->sidevel,
		ps->upvel,
		ps->upperseq,
		ps->upperframe,
		ps->lowerseq,
		ps->lowerframe,
		ps->weapon,
		ps->weaponready,
		ps->mins[0], ps->mins[1], ps->mins[2],
		ps->maxs[0], ps->maxs[1], ps->maxs[2]);
	if (cl.playernum >= 0 && cl.playernum + 1 < MAX_EDICTS)
	{
		const centity_t* cent = &cl_entities[cl.playernum + 1];
		CL_InputWatchFilePrintf(f, "  playerent serverframe=%i current_origin=(%.1f %.1f %.1f) current_angles=(%.1f %.1f %.1f) current_frame=%i model=%i solid=0x%x effects=0x%x renderfx=0x%x\n",
			cent->serverframe,
			cent->current.origin[0], cent->current.origin[1], cent->current.origin[2],
			cent->current.angles[0], cent->current.angles[1], cent->current.angles[2],
			cent->current.frame,
			cent->current.modelindex,
			cent->current.solid,
			cent->current.effects,
			cent->current.renderfx);
	}
	CL_InputWatchFilePrintf(f, "  keys");
	CL_InputWatchPrintKey(f, "forward", &in_forward);
	CL_InputWatchPrintKey(f, "back", &in_back);
	CL_InputWatchPrintKey(f, "left", &in_moveleft);
	CL_InputWatchPrintKey(f, "right", &in_moveright);
	CL_InputWatchPrintKey(f, "attack", &in_attack);
	CL_InputWatchPrintKey(f, "action", &in_action);
	CL_InputWatchPrintKey(f, "speed", &in_speed);
	CL_InputWatchFilePrintf(f, "\n");
	CL_InputWatchFilePrintf(f, "  playerinfo buttons=0x%x old=0x%x remember=0x%x upper=%i/%i lower=%i/%i weapon=%s ready=%i pm_flags=0x%x flags=0x%x\n",
		cl.playerinfo.buttons,
		cl.playerinfo.oldbuttons,
		cl.playerinfo.remember_buttons,
		cl.playerinfo.upperseq,
		cl.playerinfo.upperframe,
		cl.playerinfo.lowerseq,
		cl.playerinfo.lowerframe,
		(cl.playerinfo.pers.weapon != NULL ? cl.playerinfo.pers.weapon->classname : "<none>"),
		cl.playerinfo.pers.weaponready,
		cl.playerinfo.pm_flags,
		cl.playerinfo.flags);
	CL_InputWatchCollision(f, cmd);
	CL_InputWatchFilePrintf(f, "\n");

	fclose(f);
}

static void CL_InputWatchMaybeLog(const usercmd_t* cmd)
{
	if (cl_inputwatch == NULL || !(int)cl_inputwatch->value || cls.state != ca_active || cls.serverProtocol != PROTOCOL_VERSION)
		return;

	if (cls.realtime < inputwatch_next_time)
		return;

	inputwatch_next_time = cls.realtime + 1000;
	CL_InputWatchLog(cmd);
}

static qboolean CL_Protocol51HasMoveInput(const usercmd_t* cmd)
{
	return (cmd->forwardmove != 0 || cmd->sidemove != 0 || cmd->upmove != 0);
}

static qboolean CL_Protocol51OriginChanged(const pmove_state_t* pmove)
{
	return (protocol51_unstick_origin[0] != pmove->origin[0] ||
		protocol51_unstick_origin[1] != pmove->origin[1] ||
		protocol51_unstick_origin[2] != pmove->origin[2]);
}

static void CL_Protocol51UnstickMaybe(usercmd_t* cmd)
{
	if (cl_protocol51_unstick == NULL || !(int)cl_protocol51_unstick->value || cls.state != ca_active || cls.serverProtocol != PROTOCOL_VERSION)
		return;

	const pmove_state_t* pmove = &cl.frame.playerstate.pmove;
	if (CL_Protocol51OriginChanged(pmove))
	{
		protocol51_unstick_origin[0] = pmove->origin[0];
		protocol51_unstick_origin[1] = pmove->origin[1];
		protocol51_unstick_origin[2] = pmove->origin[2];
		protocol51_unstick_origin_time = cls.realtime;
		protocol51_unstick_recovery_until = 0;
		return;
	}

	if (!CL_Protocol51HasMoveInput(cmd) || CL_IgnoreInput() || cls.key_dest != key_game)
	{
		protocol51_unstick_origin_time = cls.realtime;
		protocol51_unstick_recovery_until = 0;
		return;
	}

	if (protocol51_unstick_origin_time == 0)
	{
		protocol51_unstick_origin_time = cls.realtime;
		return;
	}

	if (cls.realtime - protocol51_unstick_origin_time < 700)
		return;

	if (protocol51_unstick_recovery_until < cls.realtime)
	{
		protocol51_unstick_recovery_until = cls.realtime + 1500;

		if (cls.realtime - protocol51_unstick_message_time > 5000)
		{
			Com_Printf("Protocol 51 movement appears stuck; trying recovery wobble.\n");
			protocol51_unstick_message_time = cls.realtime;
		}
	}

	if (cls.realtime >= protocol51_unstick_recovery_until)
		return;

	switch ((cls.realtime / 150) % 6)
	{
		case 0:
			cmd->forwardmove = 64;
			cmd->sidemove = 64;
			break;
		case 1:
			cmd->forwardmove = 64;
			cmd->sidemove = -64;
			break;
		case 2:
			cmd->forwardmove = -64;
			cmd->sidemove = 0;
			break;
		case 3:
			cmd->forwardmove = 0;
			cmd->sidemove = 64;
			break;
		case 4:
			cmd->forwardmove = 0;
			cmd->sidemove = -64;
			break;
		default:
			cmd->forwardmove = 64;
			cmd->sidemove = 0;
			break;
	}

	cmd->buttons |= BUTTON_ANY;
}

static void CL_UnstickInput_f(void)
{
	Key_ClearStates();

	CL_ClearKButton(&in_left);
	CL_ClearKButton(&in_right);
	CL_ClearKButton(&in_forward);
	CL_ClearKButton(&in_back);
	CL_ClearKButton(&in_lookup);
	CL_ClearKButton(&in_lookdown);
	CL_ClearKButton(&in_moveleft);
	CL_ClearKButton(&in_moveright);
	CL_ClearKButton(&in_up);
	CL_ClearKButton(&in_down);
	CL_ClearKButton(&in_attack);
	CL_ClearKButton(&in_defend);
	CL_ClearKButton(&in_action);
	CL_ClearKButton(&in_creep);
	CL_ClearKButton(&in_inventory);
	CL_ClearKButton(&in_quickturn);
	CL_ClearKButton(&in_autoaim);
	CL_ClearKButton(&in_klook);
	CL_ClearKButton(&in_strafe);
	CL_ClearKButton(&in_speed);
	CL_ClearKButton(&in_lookaround);

	command_down = false;
	quickturn_time = 0.0f;
	in_do_autoaim = false;
	cls.force_packet = true;

	Com_Printf("Cleared client input button state.\n");
}

void CL_InitInput(void)
{
	Cmd_AddCommand("centerview", IN_CenterView);
	Cmd_AddCommand("stuckinfo", CL_StuckInfo_f);
	Cmd_AddCommand("cl_stuckinfo", CL_StuckInfo_f);
	Cmd_AddCommand("unstickinput", CL_UnstickInput_f);
	Cmd_AddCommand("cl_unstickinput", CL_UnstickInput_f);

	Cmd_AddCommand("+moveup", IN_UpDown);
	Cmd_AddCommand("-moveup", IN_UpUp);
	Cmd_AddCommand("+movedown", IN_DownDown);
	Cmd_AddCommand("-movedown", IN_DownUp);
	Cmd_AddCommand("+left", IN_LeftDown);
	Cmd_AddCommand("-left", IN_LeftUp);
	Cmd_AddCommand("+right", IN_RightDown);
	Cmd_AddCommand("-right", IN_RightUp);
	Cmd_AddCommand("+forward", IN_ForwardDown);
	Cmd_AddCommand("-forward", IN_ForwardUp);
	Cmd_AddCommand("+back", IN_BackDown);
	Cmd_AddCommand("-back", IN_BackUp);
	Cmd_AddCommand("+lookup", IN_LookupDown);
	Cmd_AddCommand("-lookup", IN_LookupUp);
	Cmd_AddCommand("+lookdown", IN_LookdownDown);
	Cmd_AddCommand("-lookdown", IN_LookdownUp);
	Cmd_AddCommand("+strafe", IN_StrafeDown);
	Cmd_AddCommand("-strafe", IN_StrafeUp);
	Cmd_AddCommand("+moveleft", IN_MoveleftDown);
	Cmd_AddCommand("-moveleft", IN_MoveleftUp);
	Cmd_AddCommand("+moveright", IN_MoverightDown);
	Cmd_AddCommand("-moveright", IN_MoverightUp);
	Cmd_AddCommand("+attack", IN_AttackDown);
	Cmd_AddCommand("-attack", IN_AttackUp);
	Cmd_AddCommand("+defend", IN_DefendDown);
	Cmd_AddCommand("-defend", IN_DefendUp);
	Cmd_AddCommand("+action", IN_ActionDown);
	Cmd_AddCommand("-action", IN_ActionUp);
	Cmd_AddCommand("+creep", IN_CreepDown);
	Cmd_AddCommand("-creep", IN_CreepUp);
	Cmd_AddCommand("+autoaim", IN_AutoaimDown);
	Cmd_AddCommand("-autoaim", IN_AutoaimUp);
	Cmd_AddCommand("+klook", IN_KLookDown);
	Cmd_AddCommand("-klook", IN_KLookUp);
	Cmd_AddCommand("+speed", IN_SpeedDown);
	Cmd_AddCommand("-speed", IN_SpeedUp);
	Cmd_AddCommand("+lookaround", IN_LookaroundDown);
	Cmd_AddCommand("-lookaround", IN_LookaroundUp);
	Cmd_AddCommand("+command", IN_CommandDown);
	Cmd_AddCommand("-command", IN_CommandUp);
	Cmd_AddCommand("+quickturn", IN_QuickturnDown);
	Cmd_AddCommand("-quickturn", IN_QuickturnUp);
	Cmd_AddCommand("+inventory", IN_InventoryDown);
	Cmd_AddCommand("-inventory", IN_InventoryUp);

	cl_nodelta = Cvar_Get("cl_nodelta", "0", 0);
	cl_inputwatch = Cvar_Get("cl_inputwatch", "1", 0);
	cl_protocol51_unstick = Cvar_Get("cl_protocol51_unstick", "0", CVAR_ARCHIVE);
	cl_protocol51_strip_any = Cvar_Get("cl_protocol51_strip_any", "1", CVAR_ARCHIVE);
	cl_protocol51_nodelta = Cvar_Get("cl_protocol51_nodelta", "1", CVAR_ARCHIVE);
}

static void CL_ClampPitch(void)
{
	float pitch = SHORT2ANGLE(cl.frame.playerstate.pmove.delta_angles[PITCH]);

	if (pitch > 180.0f)
		pitch -= 360.0f;

	// H2. Clamp input angles.
	cl.inputangles[PITCH] = Clamp(cl.inputangles[PITCH], -89.0f - pitch, 89.0f - pitch);

	// Clamp view angles.
	cl.viewangles[PITCH] = Clamp(cl.viewangles[PITCH], -89.0f - pitch, 89.0f - pitch);
}

// Updates cl.inputangles, cl.viewangles and cl.delta_inputangles (occasionally).
//mxd. Part of CL_FinishMove() in original logic. Separated, because we need to call this on renderframe.
//TODO: rename 'st_unknown' cvars, check conditions & annotate of all angles calculation branches when we get to playable state.
static void CL_UpdateClientAngles(void)
{
	static qboolean st_unknown1;
	static qboolean st_unknown2;
	static qboolean st_unknown3;
	static qboolean st_unknown4;
	static qboolean st_unknown5;
	static qboolean st_unknown6;

	const qboolean do_lookaround = ((in_lookaround.state & KS_DOWN) != 0);

	// Look around key pressed?
	if (do_lookaround)
	{
		cl.lookangles[PITCH] += cl.delta_inputangles[PITCH];
		cl.lookangles[YAW] += cl.delta_inputangles[YAW];
	}
	else
	{
		cl.lookangles[PITCH] = cl.viewangles[PITCH];
		cl.lookangles[YAW] = cl.viewangles[YAW];
	}

	// Can't do much when chicken...
	if (cl_entities[cl.playernum + 1].current.effects & EF_CHICKEN)
	{
		if (!do_lookaround)
		{
			cl.viewangles[PITCH] += cl.delta_inputangles[PITCH];
			cl.inputangles[YAW] = cl.viewangles[YAW] + cl.delta_inputangles[YAW];
			cl.viewangles[YAW] = cl.inputangles[YAW];
		}

		return;
	}

	// When in water.
	if (pred_pm_w_flags != 0)
	{
		if (pred_pm_w_flags & (WF_SURFACE | WF_DIVE))
		{
			st_unknown3 = true;

			if (st_unknown1)
			{
				st_unknown1 = false;
				cl.viewangles[YAW] = cl.inputangles[YAW];
				cl.delta_inputangles[YAW] = 0.0f;
			}

			if (st_unknown2)
			{
				st_unknown2 = false;
				cl.inputangles[PITCH] = -SHORT2ANGLE(cl.frame.playerstate.pmove.delta_angles[PITCH]);
				cl.viewangles[PITCH] = cl.inputangles[PITCH];
				cl.delta_inputangles[PITCH] = 0.0f;
			}
		}
		else
		{
			st_unknown2 = true;

			if (st_unknown3 && (pred_pm_w_flags & WF_SWIMFREE))
			{
				st_unknown3 = false;
				cl.viewangles[PITCH] = cl.inputangles[PITCH];
			}

			if (st_unknown1 && (pred_pm_w_flags & WF_SWIMFREE))
			{
				st_unknown1 = false;
				cl.inputangles[PITCH] = cl.viewangles[PITCH];
				cl.delta_inputangles[PITCH] = 0.0f;
			}
		}

		if (!do_lookaround)
		{
			cl.inputangles[PITCH] += cl.delta_inputangles[PITCH];
			cl.inputangles[YAW] += cl.delta_inputangles[YAW];

			cl.viewangles[PITCH] += cl.delta_inputangles[PITCH];
			cl.viewangles[YAW] += cl.delta_inputangles[YAW];
		}

		return;
	}

	// When on land.
	st_unknown1 = true;

	// When not standing in place.
	if (!(pred_pm_flags & PMF_STANDSTILL))
	{
		st_unknown5 = true;

		if (st_unknown4)
		{
			st_unknown4 = false;
			cl.inputangles[YAW] = cl.viewangles[YAW];
		}

		if (!do_lookaround)
		{
			if (!(pred_pm_flags & (PMF_LOCKTURN | PMF_TIME_TELEPORT))) //mxd. Also don't rotate model during morph/teleport sequences.
			{
				cl.inputangles[YAW] += cl.delta_inputangles[YAW];
				cl.viewangles[YAW] += cl.delta_inputangles[YAW];
			}

			cl.viewangles[PITCH] += cl.delta_inputangles[PITCH];
		}

		return;
	}

	// When executing quickturn.
	const float quickturn_rate = (CL_PREDICT ? cl.playerinfo.quickturn_rate : cl.frame.playerstate.quickturn_rate);

	if (quickturn_time > 0.0f && quickturn_rate != 0.0f)
	{
		quickturn_time -= cls.nframetime;

		float offset_fix = 0.0f;
		if (quickturn_time < 0.0f)
		{
			offset_fix = quickturn_rate * quickturn_time;
			quickturn_time = 0.0f;
		}

		const float offset = cls.nframetime * quickturn_rate + offset_fix;
		cl.inputangles[YAW] += offset;
		cl.viewangles[YAW] += offset;

		return;
	}

	// Default camera movement logic (???)
	st_unknown4 = true;

	if (st_unknown5)
	{
		st_unknown5 = false;
		cl.inputangles[PITCH] = -SHORT2ANGLE(cl.frame.playerstate.pmove.delta_angles[PITCH]);
		cl.inputangles[YAW] = cl.viewangles[YAW];
	}

	if (!do_lookaround)
	{
		cl.viewangles[PITCH] += cl.delta_inputangles[PITCH];
		cl.viewangles[YAW] += cl.delta_inputangles[YAW];
	}

	const float delta_yaw = cl.viewangles[YAW] - cl.inputangles[YAW];

	if (delta_yaw > 66.0f || delta_yaw < -66.0f)
		st_unknown6 = true;
	else if (!st_unknown6)
		return;

	if (cl.delta_inputangles[YAW] != 0.0f)
	{
		if ((cl.delta_inputangles[YAW] > 0.0f && cl.old_delta_inputangles[YAW] >= 0.0f) ||
			(cl.delta_inputangles[YAW] < 0.0f && cl.old_delta_inputangles[YAW] <= 0.0f))
		{
			cl.inputangles[YAW] += cl.delta_inputangles[YAW];
		}
		else
		{
			st_unknown6 = false;
		}
		
		return;
	}

	if (delta_yaw >= 0.0f)
	{
		if (delta_yaw > 0.0f)
		{
			if (cl.inputangles[YAW] > cl.viewangles[YAW])
			{
				st_unknown6 = false;
				return;
			}

			cl.inputangles[YAW] += delta_yaw * 0.25f;

			if (fabsf(cl.viewangles[YAW] - cl.inputangles[YAW]) >= 5.0f)
				return;

			cl.inputangles[YAW] = cl.viewangles[YAW];
		}

		st_unknown6 = false;
		return;
	}

	if (cl.inputangles[YAW] >= cl.viewangles[YAW])
	{
		cl.inputangles[YAW] += delta_yaw * 0.25f;

		if (fabsf(cl.viewangles[YAW] - cl.inputangles[YAW]) < 5.0f)
		{
			st_unknown6 = false;
			cl.inputangles[YAW] = cl.viewangles[YAW];
		}
	}
	else
	{
		st_unknown6 = false;
	}
}

void CL_RefreshCmd(void) // YQ2. Called on packetframe or renderframe.
{
	// Calculate delta.
	frame_msec = min(200, sys_frame_time - old_sys_frame_time);

	// Check bounds.
	if (frame_msec < 1)
		return;

	// CMD to fill.
	usercmd_t* cmd = &cl.cmds[cls.netchan.outgoing_sequence & (CMD_BACKUP - 1)];

	// Add movement.
	CL_BaseMove(cmd);
	IN_Move(cmd);
	CL_UpdateClientAngles(); //mxd

	// Clamp angels for prediction.
	CL_ClampPitch();

	// Store angles in usercmd.
	for (int i = 0; i < 3; i++)
	{
		cmd->angles[i] = ANGLE2SHORT(cl.inputangles[i]);
		cmd->aimangles[i] = (short)(cl.frame.playerstate.pmove.delta_angles[i] - ANGLE2SHORT(-cl.viewangles[i]));
		cmd->camera_vieworigin[i] = POS2SHORT(cl.camera_vieworigin[i]); //mxd. Use define.
		cmd->camera_viewangles[i] = ANGLE2SHORT(cl.camera_viewangles[i]);
	}

	// Update time for prediction.
	int ms = (int)(cls.nframetime * 1000.0f);

	if (ms > 250)
		ms = 100;

	cmd->msec = (byte)ms;

	// Update frame time for the next call.
	old_sys_frame_time = sys_frame_time;

	// Important events are send immediately.
	if ((in_attack.state & KS_IMPULSE_DOWN) || (in_defend.state & KS_IMPULSE_DOWN) || (in_action.state & KS_IMPULSE_DOWN))
		cls.force_packet = true;
}

void CL_RefreshMove(void) // YQ2. Called on packetframe or renderframe.
{
	// Calculate delta.
	frame_msec = min(200, sys_frame_time - old_sys_frame_time);

	// Check bounds.
	if (frame_msec < 1)
		return;

	// CMD to fill.
	usercmd_t* cmd = &cl.cmds[cls.netchan.outgoing_sequence & (CMD_BACKUP - 1)];

	// Add movement.
	CL_BaseMove(cmd);
	IN_Move(cmd);

	// Update frame time for the next call.
	old_sys_frame_time = sys_frame_time;
}

// Q2 counterpart
// Returns the fraction of the frame that the key was down.
static float CL_KeyState(kbutton_t* key)
{
	key->state &= ~(KS_IMPULSE_DOWN | KS_IMPULSE_UP); // Clear impulses.

	uint msec = key->msec;
	key->msec = 0;

	if (key->state == KS_DOWN) // Still down.
	{
		msec += sys_frame_time - key->downtime;
		key->downtime = sys_frame_time;
	}

	return Clamp((float)msec / (float)frame_msec, 0.0f, 1.0f);
}

// Moves the local angle positions. Called on packetframe or renderframe.
static void CL_AdjustAngles(void)
{
	float speed;
	float scaler;

	if (in_speed.state & KS_DOWN)
		speed = cls.nframetime * cl_anglespeedkey->value;
	else
		speed = cls.nframetime;

	if (!(in_strafe.state & KS_DOWN))
	{
		scaler = speed * cl_yawspeed->value;

		cl.delta_inputangles[YAW] -= CL_KeyState(&in_right) * scaler;
		cl.delta_inputangles[YAW] += CL_KeyState(&in_left) * scaler;
	}

	scaler = speed * cl_pitchspeed->value;

	if (in_klook.state & KS_DOWN)
	{
		cl.delta_inputangles[PITCH] -= CL_KeyState(&in_forward) * scaler;
		cl.delta_inputangles[PITCH] += CL_KeyState(&in_back) * scaler;
	}

	cl.delta_inputangles[PITCH] -= CL_KeyState(&in_lookup) * scaler;
	cl.delta_inputangles[PITCH] += CL_KeyState(&in_lookdown) * scaler;
}

qboolean CL_IgnoreInput(void) //mxd
{
	return (cl.frame.playerstate.cinematicfreeze || cl.frame.playerstate.remote_id != REMOTE_ID_NONE);
}

// Send the intended movement message to the server. Called on packetframe or renderframe.
void CL_BaseMove(usercmd_t* cmd)
{
	memset(cmd, 0, sizeof(usercmd_t));

	VectorCopy(cl.delta_inputangles, cl.old_delta_inputangles);
	VectorClear(cl.delta_inputangles);

	if (CL_IgnoreInput()) //mxd. Skip when looking through remote camera.
		return;

	CL_AdjustAngles();

	if (in_strafe.state & KS_DOWN)
	{
		cmd->sidemove += (short)(CL_KeyState(&in_right) * MOVE_SCALER);
		cmd->sidemove -= (short)(CL_KeyState(&in_left) * MOVE_SCALER);
	}

	cmd->sidemove += (short)(CL_KeyState(&in_moveright) * MOVE_SCALER);
	cmd->sidemove -= (short)(CL_KeyState(&in_moveleft) * MOVE_SCALER);

	cmd->upmove += (short)(CL_KeyState(&in_up) * MOVE_SCALER);
	cmd->upmove -= (short)(CL_KeyState(&in_down) * MOVE_SCALER);

	if (!(in_klook.state & KS_DOWN))
	{
		cmd->forwardmove += (short)(CL_KeyState(&in_forward) * MOVE_SCALER);
		cmd->forwardmove -= (short)(CL_KeyState(&in_back) * MOVE_SCALER);
	}
}

static void CL_FinishMove(usercmd_t* cmd) // Called on packetframe.
{
	// Figure button bits.
	if (!CL_IgnoreInput()) //mxd. Skip when looking through remote camera.
	{
		// He attac.
		if ((in_attack.state & (KS_DOWN | KS_IMPULSE_DOWN)))
			cmd->buttons |= BUTTON_ATTACK;

		// But also protec.
		if (in_defend.state & KS_IMPULSE_DOWN) //TODO: why no KS_DOWN check here?..
			cmd->buttons |= BUTTON_DEFEND;

		// Action.
		if (in_action.state & (KS_DOWN | KS_IMPULSE_DOWN))
			cmd->buttons |= BUTTON_ACTION;

		// Run.
		//mxd. BUTTON_RUN logic is intentionally flipped when moving backwards (walk back when running (there's no 'run backwards' animation), backflip when walking)...
		if (((in_speed.state & (KS_DOWN | KS_IMPULSE_DOWN)) != (int)cl_run->value) ^ (cmd->forwardmove < 0))
			cmd->buttons |= BUTTON_RUN;

		// Creep.
		if (in_creep.state & (KS_DOWN | KS_IMPULSE_DOWN))
		{
			cmd->buttons &= ~BUTTON_RUN; //mxd. No creep-running allowed (breaks creep forward-strafing...).
			cmd->buttons |= BUTTON_CREEP;
		}

		// Autoaim.
		const qboolean do_autoaim = (cl_autoaim->value > 0.0f); //mxd
		if ((qboolean)(in_autoaim.state & (KS_DOWN | KS_IMPULSE_DOWN)) != do_autoaim)
			cmd->buttons |= BUTTON_AUTOAIM;

		in_do_autoaim = (cmd->buttons & BUTTON_AUTOAIM);

		// TR-style look around.
		if (in_lookaround.state & (KS_DOWN | KS_IMPULSE_DOWN))
			cmd->buttons |= BUTTON_LOOKAROUND;

		// TR-style quick-turn.
		if (in_quickturn.state & (KS_DOWN | KS_IMPULSE_DOWN))
		{
			cmd->buttons |= BUTTON_QUICKTURN;
			if (quickturn_time == 0.0f)
				quickturn_time = 0.5f;
		}

		// Open inventory.
		if (in_inventory.state & (KS_DOWN | KS_IMPULSE_DOWN))
			cmd->buttons |= BUTTON_INVENTORY;
	}

	// Clear processed keystates.
	in_attack.state &= ~KS_IMPULSE_DOWN;
	in_defend.state &= ~KS_IMPULSE_DOWN;
	in_action.state &= ~KS_IMPULSE_DOWN;
	in_speed.state &= ~KS_IMPULSE_DOWN;
	in_creep.state &= ~KS_IMPULSE_DOWN;
	in_autoaim.state &= ~KS_IMPULSE_DOWN;
	in_lookaround.state &= ~KS_IMPULSE_DOWN;
	in_quickturn.state &= ~KS_IMPULSE_DOWN;
	in_inventory.state &= ~KS_IMPULSE_DOWN;

	if (anykeydown > 0 && cls.key_dest == key_game)
		cmd->buttons |= BUTTON_ANY;

	if (cls.state == ca_active && cls.serverProtocol == PROTOCOL_VERSION && (int)cl_protocol51_strip_any->value)
		cmd->buttons &= ~BUTTON_ANY;

	// Send the ambient light level at the player's current position.
	cmd->lightlevel = (byte)(Clamp(cl_lightlevel->value, 0.0f, 255.0f)); //mxd. Clamp to byte range (r_lightlevel can potentially exceed 255 when player is affected by multiple dynamic lights).
}

static void CL_FinalizeCmd(void) // YQ2. Called on packetframe.
{
	static qboolean reset_input_angles = false;

	// CMD to fill.
	usercmd_t* cmd = &cl.cmds[cls.netchan.outgoing_sequence & (CMD_BACKUP - 1)];

	if (cl.frame.playerstate.cinematicfreeze) // H2
	{
		memset(cmd, 0, sizeof(*cmd));

		if (cls.esc_cinematic)
		{
			cmd->buttons |= BUTTON_ACTION;
			cls.esc_cinematic = false;
		}

		reset_input_angles = true;
	}
	else if (reset_input_angles) // H2
	{
		reset_input_angles = false;

		VectorClear(cl.inputangles);
		VectorClear(cl.delta_inputangles);
		VectorClear(cl.viewangles);
	}

	CL_FinishMove(cmd);
}

// Builds a command even if not connected. Called on packetframe.
void CL_SendCmd(void)
{
	// Save this command off for prediction.
	const int index = cls.netchan.outgoing_sequence & (CMD_BACKUP - 1);
	cl.cmd_time[index] = cls.realtime; // For netgraph ping calculation.
	CL_FinalizeCmd(); // YQ2

	CL_Protocol51UnstickMaybe(&cl.cmds[index]);
	cl.cmd = cl.cmds[index];
	CL_InputWatchMaybeLog(&cl.cmd);

	if (cls.state == ca_disconnected || cls.state == ca_connecting)
		return;

	// H2. Skip the rest of the cinematic?
	if (anykeydown > 0 && !cl.attractloop && cl.cinematictime > 0 && cls.realtime - cl.cinematictime > 1000)
		SCR_FinishCinematic();

	if (cls.state == ca_connected)
	{
		if (cls.netchan.message.cursize > 0 || curtime - cls.netchan.last_sent > 1000)
			Netchan_Transmit(&cls.netchan, 0, NULL); // Last arg is buf.data in Q2.

		return;
	}

	// Send userinfo update if needed.
	if (userinfo_modified)
	{
		userinfo_modified = false;
		MSG_WriteByte(&cls.netchan.message, clc_userinfo);
		MSG_WriteString(&cls.netchan.message, Cvar_Userinfo());
	}

	// Init sizebuffer.
	sizebuf_t buf;
	byte data[128];
	SZ_Init(&buf, data, sizeof(data));

	// Begin client move command.
	MSG_WriteByte(&buf, clc_move);

	// Save the position for a checksum byte.
	const int checksumIndex = buf.cursize;
	MSG_WriteByte(&buf, 0);

	// Let the server know what the last frame we got was, so the next message can be delta compressed.
	// For legacy protocol 51, prefer full frames by default: this avoids subtle playerstate/entity
	// delta mismatches when talking to original servers while keeping protocol 55 bandwidth efficient.
	const qboolean protocol51_nodelta = (cls.serverProtocol == PROTOCOL_VERSION && (int)cl_protocol51_nodelta->value);
	if ((int)cl_nodelta->value || protocol51_nodelta || !cl.frame.valid || cls.demowaiting)
		MSG_WriteLong(&buf, -1); // No compression
	else
		MSG_WriteLong(&buf, cl.frame.serverframe);

	const usercmd_t nullcmd = { 0 };

	// Send this and the previous cmds in the message, so if the last packet was dropped, it can be recovered.
	const usercmd_t* cmd = &cl.cmds[(cls.netchan.outgoing_sequence - 2) & (CMD_BACKUP - 1)];
	MSG_WriteDeltaUsercmd(&buf, &nullcmd, cmd);
	const usercmd_t* oldcmd = cmd;

	cmd = &cl.cmds[(cls.netchan.outgoing_sequence - 1) & (CMD_BACKUP - 1)];
	MSG_WriteDeltaUsercmd(&buf, oldcmd, cmd);
	oldcmd = cmd;

	cmd = &cl.cmds[(cls.netchan.outgoing_sequence - 0) & (CMD_BACKUP - 1)];
	MSG_WriteDeltaUsercmd(&buf, oldcmd, cmd);

	// Calculate a checksum over the move commands.
	buf.data[checksumIndex] = COM_BlockSequenceCheckByte(&buf.data[checksumIndex + 1], buf.cursize - checksumIndex - 1, cls.netchan.outgoing_sequence);

	// Deliver the message.
	Netchan_Transmit(&cls.netchan, buf.cursize, buf.data);

	// Match the original client: Netchan_Transmit() advances outgoing_sequence,
	// so clear the newly-current command slot before input accumulates into it.
	usercmd_t* next_cmd = &cl.cmds[cls.netchan.outgoing_sequence & (CMD_BACKUP - 1)];
	memset(next_cmd, 0, sizeof(*next_cmd));
}