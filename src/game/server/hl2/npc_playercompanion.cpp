//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
//=============================================================================//

#include "cbase.h"

#include "npc_playercompanion.h"

#include "combine_mine.h"
#include "fire.h"
#include "func_tank.h"
#include "globalstate.h"
#include "npcevent.h"
#include "props.h"
#include "BasePropDoor.h"

#include "ai_hint.h"
#include "ai_localnavigator.h"
#include "ai_memory.h"
#include "ai_pathfinder.h"
#include "ai_route.h"
#include "ai_senses.h"
#include "ai_squad.h"
#include "ai_squadslot.h"
#include "ai_tacticalservices.h"
#include "ai_interactions.h"
#include "filesystem.h"
#include "collisionutils.h"
#include "grenade_frag.h"
#include <KeyValues.h>
#include "physics_npc_solver.h"
#ifdef MAPBASE
#include "mapbase/GlobalStrings.h"
#include "world.h"
#endif

ConVar ai_debug_readiness("ai_debug_readiness", "0" );
ConVar ai_use_readiness("ai_use_readiness", "1" ); // 0 = off, 1 = on, 2 = on for player squad only
ConVar ai_readiness_decay( "ai_readiness_decay", "120" );// How many seconds it takes to relax completely
ConVar ai_new_aiming( "ai_new_aiming", "1" );

#ifdef COMPANION_MELEE_ATTACK
ConVar sk_companion_melee_damage("sk_companion_melee_damage", "25");
#endif

#define GetReadinessUse()	ai_use_readiness.GetInt()

extern ConVar g_debug_transitions;

#define PLAYERCOMPANION_TRANSITION_SEARCH_DISTANCE		(100*12)

int AE_COMPANION_PRODUCE_FLARE;
int AE_COMPANION_LIGHT_FLARE;
int AE_COMPANION_RELEASE_FLARE;
#if COMPANION_MELEE_ATTACK
#define AE_PC_MELEE 3

#define COMPANION_MELEE_DIST 64.0
#endif

#define MAX_TIME_BETWEEN_BARRELS_EXPLODING			5.0f
#define MAX_TIME_BETWEEN_CONSECUTIVE_PLAYER_KILLS	3.0f

//-----------------------------------------------------------------------------
// An aimtarget becomes invalid if it gets this close
//-----------------------------------------------------------------------------
#define COMPANION_AIMTARGET_NEAREST		24.0f
#define COMPANION_AIMTARGET_NEAREST_SQR	576.0f

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

BEGIN_DATADESC( CNPC_PlayerCompanion )

	DEFINE_FIELD( 	m_bMovingAwayFromPlayer, 	FIELD_BOOLEAN ),
	DEFINE_EMBEDDED( m_SpeechWatch_PlayerLooking ),
	DEFINE_EMBEDDED( m_FakeOutMortarTimer ),

// (recomputed)
//						m_bWeightPathsInCover	

// These are auto-saved by AI
//	DEFINE_FIELD( m_AssaultBehavior,	CAI_AssaultBehavior ),
//	DEFINE_FIELD( m_FollowBehavior,		CAI_FollowBehavior ),
//	DEFINE_FIELD( m_StandoffBehavior,	CAI_StandoffBehavior ),
//	DEFINE_FIELD( m_LeadBehavior,		CAI_LeadBehavior ),
//  DEFINE_FIELD( m_OperatorBehavior,	FIELD_EMBEDDED ),
//					m_ActBusyBehavior
//					m_PassengerBehavior
//					m_FearBehavior

	DEFINE_INPUTFUNC( FIELD_VOID,	"OutsideTransition",	InputOutsideTransition ),
	DEFINE_INPUTFUNC( FIELD_VOID,	"SetReadinessPanic",	InputSetReadinessPanic ),
	DEFINE_INPUTFUNC( FIELD_VOID,	"SetReadinessStealth",	InputSetReadinessStealth ),
	DEFINE_INPUTFUNC( FIELD_VOID,	"SetReadinessLow",		InputSetReadinessLow ),
	DEFINE_INPUTFUNC( FIELD_VOID,	"SetReadinessMedium",	InputSetReadinessMedium ),
	DEFINE_INPUTFUNC( FIELD_VOID,	"SetReadinessHigh",		InputSetReadinessHigh ),
	DEFINE_INPUTFUNC( FIELD_FLOAT,	"LockReadiness",		InputLockReadiness ),

//------------------------------------------------------------------------------
#ifdef HL2_EPISODIC
	DEFINE_FIELD( m_hFlare, FIELD_EHANDLE ),

	DEFINE_INPUTFUNC( FIELD_STRING,	"EnterVehicle",				InputEnterVehicle ),
	DEFINE_INPUTFUNC( FIELD_STRING, "EnterVehicleImmediately",	InputEnterVehicleImmediately ),
	DEFINE_INPUTFUNC( FIELD_VOID,	"ExitVehicle",				InputExitVehicle ),
	DEFINE_INPUTFUNC( FIELD_VOID,	"CancelEnterVehicle",		InputCancelEnterVehicle ),
#endif	// HL2_EPISODIC
//------------------------------------------------------------------------------

#ifndef MAPBASE
	DEFINE_INPUTFUNC( FIELD_STRING, "GiveWeapon",			InputGiveWeapon ),
#endif

	DEFINE_FIELD( m_flReadiness,			FIELD_FLOAT ),
	DEFINE_FIELD( m_flReadinessSensitivity,	FIELD_FLOAT ),
	DEFINE_FIELD( m_bReadinessCapable,		FIELD_BOOLEAN ),
	DEFINE_FIELD( m_flReadinessLockedUntil, FIELD_TIME ),
	DEFINE_FIELD( m_fLastBarrelExploded,	FIELD_TIME ),
	DEFINE_FIELD( m_iNumConsecutiveBarrelsExploded, FIELD_INTEGER ),
	DEFINE_FIELD( m_fLastPlayerKill, FIELD_TIME ),
	DEFINE_FIELD( m_iNumConsecutivePlayerKills, FIELD_INTEGER ),

	//					m_flBoostSpeed (recomputed)

	DEFINE_EMBEDDED( m_AnnounceAttackTimer ),

	DEFINE_FIELD( m_hAimTarget,				FIELD_EHANDLE ),

	DEFINE_KEYFIELD( m_bAlwaysTransition, FIELD_BOOLEAN, "AlwaysTransition" ),
	DEFINE_KEYFIELD( m_bDontPickupWeapons, FIELD_BOOLEAN, "DontPickupWeapons" ),

	DEFINE_INPUTFUNC( FIELD_VOID, "EnableAlwaysTransition", InputEnableAlwaysTransition ),
	DEFINE_INPUTFUNC( FIELD_VOID, "DisableAlwaysTransition", InputDisableAlwaysTransition ),

	DEFINE_INPUTFUNC( FIELD_VOID, "EnableWeaponPickup", InputEnableWeaponPickup ),
	DEFINE_INPUTFUNC( FIELD_VOID, "DisableWeaponPickup", InputDisableWeaponPickup ),


#if HL2_EPISODIC
	DEFINE_INPUTFUNC( FIELD_VOID, "ClearAllOutputs", InputClearAllOuputs ),
#endif

	DEFINE_OUTPUT( m_OnWeaponPickup, "OnWeaponPickup" ),

#ifdef MAPBASE
	DEFINE_AIGRENADE_DATADESC()
	DEFINE_INPUT( m_iGrenadeCapabilities, FIELD_INTEGER, "SetGrenadeCapabilities" ),
#endif

END_DATADESC()

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

CNPC_PlayerCompanion::eCoverType CNPC_PlayerCompanion::gm_fCoverSearchType;
bool CNPC_PlayerCompanion::gm_bFindingCoverFromAllEnemies;
#ifdef MAPBASE
string_t CNPC_PlayerCompanion::gm_iszMortarClassname;
string_t CNPC_PlayerCompanion::gm_iszGroundTurretClassname;
#else
string_t CNPC_PlayerCompanion::gm_iszMortarClassname;
string_t CNPC_PlayerCompanion::gm_iszFloorTurretClassname;
string_t CNPC_PlayerCompanion::gm_iszGroundTurretClassname;
string_t CNPC_PlayerCompanion::gm_iszShotgunClassname;
string_t CNPC_PlayerCompanion::gm_iszRollerMineClassname;
#ifdef MAPBASE
string_t CNPC_PlayerCompanion::gm_iszSMG1Classname;
string_t CNPC_PlayerCompanion::gm_iszAR2Classname;
#endif
#endif

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

bool CNPC_PlayerCompanion::CreateBehaviors()
{
#ifdef HL2_EPISODIC
	AddBehavior( &m_FearBehavior );
	AddBehavior( &m_PassengerBehavior );
#endif // HL2_EPISODIC	

	AddBehavior( &m_ActBusyBehavior );

#ifdef HL2_EPISODIC
	AddBehavior( &m_OperatorBehavior );
	AddBehavior( &m_StandoffBehavior );
	AddBehavior( &m_AssaultBehavior );
	AddBehavior( &m_FollowBehavior );
	AddBehavior( &m_LeadBehavior );
#else
	AddBehavior( &m_AssaultBehavior );
	AddBehavior( &m_StandoffBehavior );
	AddBehavior( &m_FollowBehavior );
	AddBehavior( &m_LeadBehavior );
#endif//HL2_EPISODIC
	
	return BaseClass::CreateBehaviors();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_PlayerCompanion::Precache()
{
#ifdef MAPBASE
	gm_iszMortarClassname = AllocPooledString( "func_tankmortar" );
	gm_iszGroundTurretClassname = AllocPooledString( "npc_turret_ground" );
#else
	gm_iszMortarClassname = AllocPooledString( "func_tankmortar" );
	gm_iszFloorTurretClassname = AllocPooledString( "npc_turret_floor" );
	gm_iszGroundTurretClassname = AllocPooledString( "npc_turret_ground" );
	gm_iszShotgunClassname = AllocPooledString( "weapon_shotgun" );
	gm_iszRollerMineClassname = AllocPooledString( "npc_rollermine" );
#ifdef MAPBASE
	gm_iszSMG1Classname = AllocPooledString( "weapon_smg1" );
	gm_iszAR2Classname = AllocPooledString( "weapon_ar2" );
#endif
#endif

	PrecacheModel( STRING( GetModelName() ) );
	
#ifdef HL2_EPISODIC
	// The flare we're able to pull out
	PrecacheModel( "models/props_junk/flare.mdl" );
#endif // HL2_EPISODIC

#ifdef MAPBASE
	PrecacheScriptSound( "Weapon_CombineGuard.Special1" );
#endif

	BaseClass::Precache();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_PlayerCompanion::Spawn()
{
	SelectModel();

	Precache();

	SetModel( STRING( GetModelName() ) );

	SetHullType(HULL_HUMAN);
	SetHullSizeNormal();

	SetSolid( SOLID_BBOX );
	AddSolidFlags( FSOLID_NOT_STANDABLE );
	SetBloodColor( BLOOD_COLOR_RED );
	m_flFieldOfView		= 0.02;
	m_NPCState		= NPC_STATE_NONE;

	CapabilitiesClear();
	CapabilitiesAdd( bits_CAP_SQUAD );

	if ( !HasSpawnFlags( SF_NPC_START_EFFICIENT ) )
	{
		CapabilitiesAdd( bits_CAP_ANIMATEDFACE | bits_CAP_TURN_HEAD );
		CapabilitiesAdd( bits_CAP_USE_WEAPONS | bits_CAP_AIM_GUN | bits_CAP_MOVE_SHOOT );
		CapabilitiesAdd( bits_CAP_DUCK | bits_CAP_DOORS_GROUP );
		CapabilitiesAdd( bits_CAP_USE_SHOT_REGULATOR );
	}
	CapabilitiesAdd( bits_CAP_NO_HIT_PLAYER | bits_CAP_NO_HIT_SQUADMATES | bits_CAP_FRIENDLY_DMG_IMMUNE );
	CapabilitiesAdd( bits_CAP_MOVE_GROUND );
	SetMoveType( MOVETYPE_STEP );

	m_HackedGunPos = Vector( 0, 0, 55 );
	
	SetAimTarget(NULL);
	m_bReadinessCapable = IsReadinessCapable();
	SetReadinessValue( 0.0f );
	SetReadinessSensitivity( random->RandomFloat( 0.7, 1.3 ) );
	m_flReadinessLockedUntil = 0.0f;

	m_AnnounceAttackTimer.Set( 10, 30 );

#if HL2_EPISODIC && !MAPBASE // Mapbase permits this flag since the warning can be distracting and stripping the flag might break some HL2 maps in Episodic mods
	// We strip this flag because it's been made obsolete by the StartScripting behavior
	if ( HasSpawnFlags( SF_NPC_ALTCOLLISION ) )
	{
		Warning( "NPC %s using alternate collision! -- DISABLED\n", STRING( GetEntityName() ) );
		RemoveSpawnFlags( SF_NPC_ALTCOLLISION );
	}

	m_hFlare = NULL;
#endif // HL2_EPISODIC

#if COMPANION_MELEE_ATTACK
	m_nMeleeDamage = sk_companion_melee_damage.GetInt();
#endif

	BaseClass::Spawn();
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
int CNPC_PlayerCompanion::Restore( IRestore &restore )
{
	int baseResult = BaseClass::Restore( restore );

	if ( gpGlobals->eLoadType == MapLoad_Transition )
	{
		m_StandoffBehavior.SetActive( false );
	}

#if HL2_EPISODIC && !MAPBASE // Mapbase permits this flag since the warning can be distracting and stripping the flag might break some HL2 maps in Episodic mods
	// We strip this flag because it's been made obsolete by the StartScripting behavior
	if ( HasSpawnFlags( SF_NPC_ALTCOLLISION ) )
	{
		Warning( "NPC %s using alternate collision! -- DISABLED\n", STRING( GetEntityName() ) );
		RemoveSpawnFlags( SF_NPC_ALTCOLLISION );
	}
#endif // HL2_EPISODIC

	return baseResult;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
int CNPC_PlayerCompanion::ObjectCaps() 
{ 
	int caps = UsableNPCObjectCaps( BaseClass::ObjectCaps() );
	return caps; 
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CNPC_PlayerCompanion::ShouldAlwaysThink() 
{ 
	return ( BaseClass::ShouldAlwaysThink() || ( GetFollowBehavior().GetFollowTarget() && GetFollowBehavior().GetFollowTarget()->IsPlayer() ) ); 
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
Disposition_t CNPC_PlayerCompanion::IRelationType( CBaseEntity *pTarget )
{
	if ( !pTarget )
		return D_NU;

	Disposition_t baseRelationship = BaseClass::IRelationType( pTarget );

	if ( baseRelationship != D_LI )
	{
		if ( IsTurret( pTarget ) )
		{
			// Citizens are afeared of turrets, so long as the turret
			// is active... that is, not classifying itself as CLASS_NONE
			if( pTarget->Classify() != CLASS_NONE )
			{
				if( !hl2_episodic.GetBool() && IsSafeFromFloorTurret(GetAbsOrigin(), pTarget) )
				{
					return D_NU;
				}

				return D_FR;
			}
		}
		else if ( baseRelationship == D_HT && 
				  pTarget->IsNPC() && 
				  ((CAI_BaseNPC *)pTarget)->GetActiveWeapon() && 
#ifdef MAPBASE
				  (EntIsClass( ((CAI_BaseNPC *)pTarget)->GetActiveWeapon(), gm_iszShotgunClassname ) &&
				  ( !GetActiveWeapon() || !EntIsClass( GetActiveWeapon(), gm_iszShotgunClassname ) ) ) )
#else
				  ((CAI_BaseNPC *)pTarget)->GetActiveWeapon()->ClassMatches( gm_iszShotgunClassname ) &&
				  ( !GetActiveWeapon() || !GetActiveWeapon()->ClassMatches( gm_iszShotgunClassname ) ) )
#endif
		{
			if ( (pTarget->GetAbsOrigin() - GetAbsOrigin()).LengthSqr() < Square( 25 * 12 ) )
			{
				// Ignore enemies on the floor above us
				if ( fabs(pTarget->GetAbsOrigin().z - GetAbsOrigin().z) < 100 )
					return D_FR;
			}
		}
	}

	return baseRelationship;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CNPC_PlayerCompanion::IsSilentSquadMember() const
{
	if ( (const_cast<CNPC_PlayerCompanion *>(this))->Classify() == CLASS_PLAYER_ALLY_VITAL && m_pSquad && MAKE_STRING(m_pSquad->GetName()) == GetPlayerSquadName() )
	{
		return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_PlayerCompanion::GatherConditions()
{
	BaseClass::GatherConditions();

	if ( AI_IsSinglePlayer() )
	{
		CBasePlayer *pPlayer = UTIL_GetLocalPlayer();

		if ( Classify() == CLASS_PLAYER_ALLY_VITAL )
		{
			bool bInPlayerSquad = ( m_pSquad && MAKE_STRING(m_pSquad->GetName()) == GetPlayerSquadName() );
			if ( bInPlayerSquad )
			{
				if ( GetState() == NPC_STATE_SCRIPT || ( !HasCondition( COND_SEE_PLAYER ) && (GetAbsOrigin() - pPlayer->GetAbsOrigin()).LengthSqr() > Square(50 * 12) ) )
				{
					RemoveFromSquad();
				}
			}
			else if ( GetState() != NPC_STATE_SCRIPT )
			{
				if ( HasCondition( COND_SEE_PLAYER ) && (GetAbsOrigin() - pPlayer->GetAbsOrigin()).LengthSqr() < Square(25 * 12) )
				{
					if ( hl2_episodic.GetBool() )
					{
						// Don't stomp our squad if we're in one
						if ( GetSquad() == NULL )
						{
							AddToSquad( GetPlayerSquadName() );
						}
					}
					else
					{
						AddToSquad( GetPlayerSquadName() );
					}
				}
			}
		}

		m_flBoostSpeed = 0;

		if ( m_AnnounceAttackTimer.Expired() &&
			 ( GetLastEnemyTime() == 0.0 || gpGlobals->curtime - GetLastEnemyTime() > 20 ) )
		{
			// Always delay when an encounter begins
			m_AnnounceAttackTimer.Set( 4, 8 );
		}

		if ( GetFollowBehavior().GetFollowTarget() && 
			 ( GetFollowBehavior().GetFollowTarget()->IsPlayer() || GetCommandGoal() != vec3_invalid ) && 
			 GetFollowBehavior().IsMovingToFollowTarget() && 
			 GetFollowBehavior().GetGoalRange() > 0.1 &&
			 BaseClass::GetIdealSpeed() > 0.1 )
		{
			Vector vPlayerToFollower = GetAbsOrigin() - pPlayer->GetAbsOrigin();
			float dist = vPlayerToFollower.NormalizeInPlace();

			bool bDoSpeedBoost = false;
			if ( !HasCondition( COND_IN_PVS ) )
				bDoSpeedBoost = true;
			else if ( GetFollowBehavior().GetFollowTarget()->IsPlayer() )
			{
				if ( dist > GetFollowBehavior().GetGoalRange() * 2 )
				{
					float dot = vPlayerToFollower.Dot( pPlayer->EyeDirection3D() );
					if ( dot < 0 )
					{
						bDoSpeedBoost = true;
					}
				}
			}

			if ( bDoSpeedBoost )
			{
				float lag = dist / GetFollowBehavior().GetGoalRange();

				float mult;
				
				if ( lag > 10.0 )
					mult = 2.0;
				else if ( lag > 5.0 )
					mult = 1.5;
				else if ( lag > 3.0 )
					mult = 1.25;
				else
					mult = 1.1;

				m_flBoostSpeed = pPlayer->GetSmoothedVelocity().Length();

				if ( m_flBoostSpeed < BaseClass::GetIdealSpeed() )
					m_flBoostSpeed = BaseClass::GetIdealSpeed();

				m_flBoostSpeed *= mult;
			}
		}
	}

	// Update our readiness if we're 
	if ( IsReadinessCapable() )
	{
		UpdateReadiness();
	}

	PredictPlayerPush();

	// Grovel through memories, don't forget enemies parented to func_tankmortar entities.
	// !!!LATER - this should really call out and ask if I want to forget the enemy in question.
	AIEnemiesIter_t	iter;
	for( AI_EnemyInfo_t *pMemory = GetEnemies()->GetFirst(&iter); pMemory != NULL; pMemory = GetEnemies()->GetNext(&iter) )
	{
		if ( IsMortar( pMemory->hEnemy ) || IsSniper( pMemory->hEnemy ) )
		{
			pMemory->bUnforgettable = ( IRelationType( pMemory->hEnemy ) < D_LI );
			pMemory->bEludedMe = false;
		}
	}

	if ( GetMotor()->IsDeceleratingToGoal() && IsCurTaskContinuousMove() && 
		 HasCondition( COND_PLAYER_PUSHING) && IsCurSchedule( SCHED_MOVE_AWAY ) )
	{
		ClearSchedule( "Being pushed by player" );
	}

	CBaseEntity *pEnemy = GetEnemy();
	m_bWeightPathsInCover = false;
	if ( pEnemy )
	{
		if ( IsMortar( pEnemy ) || IsSniper( pEnemy ) )
		{
			m_bWeightPathsInCover = true;
		}
	}

	ClearCondition( COND_PC_SAFE_FROM_MORTAR );
	if ( IsCurSchedule( SCHED_TAKE_COVER_FROM_BEST_SOUND ) )
	{
		CSound *pSound = GetBestSound( SOUND_DANGER );

		if ( pSound && (pSound->SoundType() & SOUND_CONTEXT_MORTAR) )
		{
			float flDistSq = (pSound->GetSoundOrigin() - GetAbsOrigin() ).LengthSqr();
			if ( flDistSq > Square( MORTAR_BLAST_RADIUS + GetHullWidth() * 2 ) )
				SetCondition( COND_PC_SAFE_FROM_MORTAR );
		}
	}
	
	// Handle speech AI. Don't do AI speech if we're in scripts unless permitted by the EnableSpeakWhileScripting input.
	if ( m_NPCState == NPC_STATE_IDLE || m_NPCState == NPC_STATE_ALERT || m_NPCState == NPC_STATE_COMBAT ||
		( ( m_NPCState == NPC_STATE_SCRIPT ) && CanSpeakWhileScripting() ) )
	{
		DoCustomSpeechAI();
	}

#ifdef MAPBASE
	// Alyx's custom combat AI copied to CNPC_PlayerCompanion for reasons specified in said function.
	if ( m_NPCState == NPC_STATE_COMBAT )
	{
		DoCustomCombatAI();
	}
#endif

	if ( AI_IsSinglePlayer() && hl2_episodic.GetBool() && !GetEnemy() && HasCondition( COND_HEAR_PLAYER ) )
	{
		Vector los = ( UTIL_GetLocalPlayer()->EyePosition() - EyePosition() );
		los.z = 0;
		VectorNormalize( los );

		if ( DotProduct( los, EyeDirection2D() ) > DOT_45DEGREE )
		{
			ClearCondition( COND_HEAR_PLAYER );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_PlayerCompanion::DoCustomSpeechAI( void )
{
	CBasePlayer *pPlayer = AI_GetSinglePlayer();
	
	// Don't allow this when we're getting in the car
#ifdef HL2_EPISODIC
	bool bPassengerInTransition = ( IsInAVehicle() && ( m_PassengerBehavior.GetPassengerState() == PASSENGER_STATE_ENTERING || m_PassengerBehavior.GetPassengerState() == PASSENGER_STATE_EXITING ) );
#else
	bool bPassengerInTransition = false;
#endif

	Vector vecEyePosition = EyePosition();
	if ( bPassengerInTransition == false && pPlayer && pPlayer->FInViewCone( vecEyePosition ) && pPlayer->FVisible( vecEyePosition ) )
	{
		if ( m_SpeechWatch_PlayerLooking.Expired() )
		{
			SpeakIfAllowed( TLK_LOOK );
			m_SpeechWatch_PlayerLooking.Stop();
		}
	}
	else
	{
		m_SpeechWatch_PlayerLooking.Start( 1.0f );
	}	

	// Mention the player is dead
#ifdef MAPBASE
	// (unless we hate them)
	if ( HasCondition( COND_TALKER_PLAYER_DEAD ) && (!pPlayer || IRelationType(pPlayer) > D_FR) )
#else
	if ( HasCondition( COND_TALKER_PLAYER_DEAD ) )
#endif
	{
		SpeakIfAllowed( TLK_PLDEAD );
	}
}

//-----------------------------------------------------------------------------
void CNPC_PlayerCompanion::PredictPlayerPush()
{
	CBasePlayer *pPlayer = AI_GetSinglePlayer();
	if ( pPlayer && pPlayer->GetSmoothedVelocity().LengthSqr() >= Square(140))
	{
		Vector predictedPosition = pPlayer->WorldSpaceCenter() + pPlayer->GetSmoothedVelocity() * .4;
		Vector delta = WorldSpaceCenter() - predictedPosition;
		if ( delta.z < GetHullHeight() * .5 && delta.Length2DSqr() < Square(GetHullWidth() * 1.414)  )
			TestPlayerPushing( pPlayer );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Allows for modification of the interrupt mask for the current schedule.
//			In the most cases the base implementation should be called first.
//-----------------------------------------------------------------------------
void CNPC_PlayerCompanion::BuildScheduleTestBits()
{
	BaseClass::BuildScheduleTestBits();
	
	// Always interrupt to get into the car
	SetCustomInterruptCondition( COND_PC_BECOMING_PASSENGER );

	if ( IsCurSchedule(SCHED_RANGE_ATTACK1) )
	{
		SetCustomInterruptCondition( COND_PLAYER_PUSHING );
	}

#if COMPANION_MELEE_ATTACK
	if (IsCurSchedule(SCHED_RANGE_ATTACK1) ||
		IsCurSchedule(SCHED_BACK_AWAY_FROM_ENEMY) ||
		IsCurSchedule(SCHED_RUN_FROM_ENEMY))
	{
		SetCustomInterruptCondition( COND_CAN_MELEE_ATTACK1 );
	}
#endif

	if ( ( ConditionInterruptsCurSchedule( COND_GIVE_WAY ) || 
		   IsCurSchedule(SCHED_HIDE_AND_RELOAD ) || 
		   IsCurSchedule(SCHED_RELOAD ) || 
		   IsCurSchedule(SCHED_STANDOFF ) || 
		   IsCurSchedule(SCHED_TAKE_COVER_FROM_ENEMY ) || 
		   IsCurSchedule(SCHED_COMBAT_FACE ) || 
		   IsCurSchedule(SCHED_ALERT_FACE )  ||
		   IsCurSchedule(SCHED_COMBAT_STAND ) || 
		   IsCurSchedule(SCHED_ALERT_FACE_BESTSOUND) ||
		   IsCurSchedule(SCHED_ALERT_STAND) ) )
	{
		SetCustomInterruptCondition( COND_HEAR_MOVE_AWAY );
		SetCustomInterruptCondition( COND_PLAYER_PUSHING );
		SetCustomInterruptCondition( COND_PC_HURTBYFIRE );
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
CSound *CNPC_PlayerCompanion::GetBestSound( int validTypes )
{
	AISoundIter_t iter;

	CSound *pCurrentSound = GetSenses()->GetFirstHeardSound( &iter );
	while ( pCurrentSound )
	{
		// the npc cares about this sound, and it's close enough to hear.
		if ( pCurrentSound->FIsSound() )
		{
			if( pCurrentSound->SoundContext() & SOUND_CONTEXT_MORTAR )
			{
				return pCurrentSound;
			}
		}

		pCurrentSound = GetSenses()->GetNextHeardSound( &iter );
	}

	return BaseClass::GetBestSound( validTypes );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CNPC_PlayerCompanion::QueryHearSound( CSound *pSound )
{
	if( !BaseClass::QueryHearSound(pSound) )
		return false;

	switch( pSound->SoundTypeNoContext() )
	{
	case SOUND_READINESS_LOW:
		SetReadinessLevel( AIRL_RELAXED, false, true );
		return false;

	case SOUND_READINESS_MEDIUM:
		SetReadinessLevel( AIRL_STIMULATED, false, true );
		return false;

	case SOUND_READINESS_HIGH:
		SetReadinessLevel( AIRL_AGITATED, false, true );
		return false;

	default:
		return true;
	}
}

//-----------------------------------------------------------------------------

bool CNPC_PlayerCompanion::QuerySeeEntity( CBaseEntity *pEntity, bool bOnlyHateOrFearIfNPC )
{
	CAI_BaseNPC *pOther = pEntity->MyNPCPointer(); 
	if ( pOther && 
		 ( pOther->GetState() == NPC_STATE_ALERT || GetState() == NPC_STATE_ALERT ||  pOther->GetState() == NPC_STATE_COMBAT || GetState() == NPC_STATE_COMBAT ) && 
		 pOther->IsPlayerAlly() )
	{
		return true;
	}

	return BaseClass::QuerySeeEntity( pEntity, bOnlyHateOrFearIfNPC );
}



//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CNPC_PlayerCompanion::ShouldIgnoreSound( CSound *pSound )
{
	if ( !BaseClass::ShouldIgnoreSound( pSound ) )
	{
		if ( pSound->IsSoundType( SOUND_DANGER ) && !SoundIsVisible(pSound) )
			return true;

#ifdef HL2_EPISODIC
		// Ignore vehicle sounds when we're driving in them
		if ( pSound->m_hOwner && pSound->m_hOwner->GetServerVehicle() != NULL )
		{
			if ( m_PassengerBehavior.GetPassengerState() == PASSENGER_STATE_INSIDE && 
				m_PassengerBehavior.GetTargetVehicle() == pSound->m_hOwner->GetServerVehicle()->GetVehicleEnt() )
				return true;
		}
#endif // HL2_EPISODIC
	}

	return false;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
int CNPC_PlayerCompanion::SelectSchedule()
{
	m_bMovingAwayFromPlayer = false;

#ifdef HL2_EPISODIC
	// Always defer to passenger if it's running
	if ( ShouldDeferToPassengerBehavior() )
	{
		DeferSchedulingToBehavior( &m_PassengerBehavior );
		return BaseClass::SelectSchedule();
	}
#endif // HL2_EPISODIC

	if ( m_ActBusyBehavior.IsRunning() && m_ActBusyBehavior.NeedsToPlayExitAnim() )
	{
		trace_t tr;
		Vector	vUp = GetAbsOrigin();
		vUp.z += .25;

		AI_TraceHull( GetAbsOrigin(), vUp, GetHullMins(),
			GetHullMaxs(), MASK_SOLID, this, COLLISION_GROUP_NONE, &tr );

		if ( tr.startsolid )
		{
			if ( HasCondition( COND_HEAR_DANGER ) )
			{
				m_ActBusyBehavior.StopBusying();
			}
			DeferSchedulingToBehavior( &m_ActBusyBehavior );
			return BaseClass::SelectSchedule();
		}
	}

#ifdef MAPBASE
	if ( m_hForcedGrenadeTarget )
	{
		// Can't throw at the target, so lets try moving to somewhere where I can see it
		if ( !FVisible( m_hForcedGrenadeTarget ) )
		{
			return SCHED_PC_MOVE_TO_FORCED_GREN_LOS;
		}
		else if ( m_flNextGrenadeCheck < gpGlobals->curtime )
		{
			Vector vecTarget = m_hForcedGrenadeTarget->WorldSpaceCenter();

			// The fact we have a forced grenade target overrides whether we're marked as "capable".
			// If we're *only* alt-fire capable, use an energy ball. If not, throw a grenade.
			if (!IsAltFireCapable() || IsGrenadeCapable())
			{
				Vector vecTarget = m_hForcedGrenadeTarget->WorldSpaceCenter();
				{
					// If we can, throw a grenade at the target. 
					// Ignore grenade count / distance / etc
					if ( CheckCanThrowGrenade( vecTarget ) )
					{
						m_hForcedGrenadeTarget = NULL;
						return SCHED_PC_FORCED_GRENADE_THROW;
					}
				}
			}
			else
			{
				if ( FVisible( m_hForcedGrenadeTarget ) )
				{
					m_vecAltFireTarget = vecTarget;
					m_hForcedGrenadeTarget = NULL;
					return SCHED_PC_AR2_ALTFIRE;
				}
			}
		}
	}
#endif

	int nSched = SelectFlinchSchedule();
	if ( nSched != SCHED_NONE )
		return nSched;

	int schedule = SelectScheduleDanger();
	if ( schedule != SCHED_NONE )
		return schedule;
	
	schedule = SelectSchedulePriorityAction();
	if ( schedule != SCHED_NONE )
		return schedule;

	if ( ShouldDeferToFollowBehavior() )
	{
		DeferSchedulingToBehavior( &(GetFollowBehavior()) );
	}
	else if ( !BehaviorSelectSchedule() )
	{
		if ( m_NPCState == NPC_STATE_IDLE || m_NPCState == NPC_STATE_ALERT )
		{
			schedule = SelectScheduleNonCombat();
			if ( schedule != SCHED_NONE )
				return schedule;
		}
		else if ( m_NPCState == NPC_STATE_COMBAT )
		{
			schedule = SelectScheduleCombat();
			if ( schedule != SCHED_NONE )
				return schedule;
		}
	}

	return BaseClass::SelectSchedule();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
int CNPC_PlayerCompanion::SelectScheduleDanger()
{
	if( HasCondition( COND_HEAR_DANGER ) )
	{
		CSound *pSound;
		pSound = GetBestSound( SOUND_DANGER );

		ASSERT( pSound != NULL );

		if ( pSound && (pSound->m_iType & SOUND_DANGER) )
		{
			if ( !(pSound->SoundContext() & (SOUND_CONTEXT_MORTAR|SOUND_CONTEXT_FROM_SNIPER)) || IsOkToCombatSpeak() )
				SpeakIfAllowed( TLK_DANGER );

			if ( HasCondition( COND_PC_SAFE_FROM_MORTAR ) )
			{
				// Just duck and cover if far away from the explosion, or in cover.
				return SCHED_COWER;
			}
#if 1
			else if( pSound && (pSound->m_iType & SOUND_CONTEXT_FROM_SNIPER) )
			{
				return SCHED_COWER;
			}
#endif

			return SCHED_TAKE_COVER_FROM_BEST_SOUND;
		}
	}

	if ( GetEnemy() && 
		m_FakeOutMortarTimer.Expired() && 
		GetFollowBehavior().GetFollowTarget() && 
		IsMortar( GetEnemy() ) && 
		assert_cast<CAI_BaseNPC *>(GetEnemy())->GetEnemy() == this && 
		assert_cast<CAI_BaseNPC *>(GetEnemy())->FInViewCone( this ) &&
		assert_cast<CAI_BaseNPC *>(GetEnemy())->FVisible( this ) )
	{
		m_FakeOutMortarTimer.Set( 7 );
		return SCHED_PC_FAKEOUT_MORTAR;
	}

	if ( HasCondition( COND_HEAR_MOVE_AWAY ) )
		return SCHED_MOVE_AWAY;

	if ( HasCondition( COND_PC_HURTBYFIRE ) )
	{
		ClearCondition( COND_PC_HURTBYFIRE );
		return SCHED_MOVE_AWAY;
	}
	
	return SCHED_NONE;	
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
int CNPC_PlayerCompanion::SelectSchedulePriorityAction()
{
	if ( GetGroundEntity() && !IsInAScript() )
	{
		if ( GetGroundEntity()->IsPlayer() )
		{
			return SCHED_PC_GET_OFF_COMPANION;
		}

		if ( GetGroundEntity()->IsNPC() && 
			 IRelationType( GetGroundEntity() ) == D_LI && 
			 WorldSpaceCenter().z - GetGroundEntity()->WorldSpaceCenter().z > GetHullHeight() * .5 )
		{
			return SCHED_PC_GET_OFF_COMPANION;
		}
	}

	int schedule = SelectSchedulePlayerPush();
	if ( schedule != SCHED_NONE )
	{
		if ( GetFollowBehavior().IsRunning() )
			KeepRunningBehavior();
		return schedule;
	}

	return SCHED_NONE;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
int CNPC_PlayerCompanion::SelectSchedulePlayerPush()
{
	if ( HasCondition( COND_PLAYER_PUSHING ) && !IsInAScript() && !IgnorePlayerPushing() )
	{
		// Ignore move away before gordon becomes the man
		if ( GlobalEntity_GetState("gordon_precriminal") != GLOBAL_ON )
		{
			m_bMovingAwayFromPlayer = true;
			return SCHED_MOVE_AWAY;
		}
	}

	return SCHED_NONE;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CNPC_PlayerCompanion::IgnorePlayerPushing( void )
{
	if ( hl2_episodic.GetBool() )
	{
		// Ignore player pushes if we're leading him
		if ( m_LeadBehavior.IsRunning() && m_LeadBehavior.HasGoal() )
			return true;
		if ( m_AssaultBehavior.IsRunning() && m_AssaultBehavior.OnStrictAssault() )
			return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
int CNPC_PlayerCompanion::SelectScheduleCombat()
{
#if COMPANION_MELEE_ATTACK
	if ( HasCondition( COND_CAN_MELEE_ATTACK1 ) )
	{
		DevMsg("Returning melee attack schedule\n");
		return SCHED_MELEE_ATTACK1;
	}
#endif

	if ( CanReload() && (HasCondition ( COND_NO_PRIMARY_AMMO ) || HasCondition(COND_LOW_PRIMARY_AMMO)) )
	{
		return SCHED_HIDE_AND_RELOAD;
	}

#ifdef MAPBASE
	if ( HasGrenades() && GetEnemy() && !HasCondition(COND_SEE_ENEMY) )
	{
		// We don't see our enemy. If it hasn't been long since I last saw him,
		// and he's pretty close to the last place I saw him, throw a grenade in 
		// to flush him out. A wee bit of cheating here...

		float flTime;
		float flDist;

		flTime = gpGlobals->curtime - GetEnemies()->LastTimeSeen( GetEnemy() );
		flDist = ( GetEnemy()->GetAbsOrigin() - GetEnemies()->LastSeenPosition( GetEnemy() ) ).Length();

		//Msg("Time: %f   Dist: %f\n", flTime, flDist );
		if ( flTime <= COMBINE_GRENADE_FLUSH_TIME && flDist <= COMBINE_GRENADE_FLUSH_DIST && CanGrenadeEnemy( false ) && OccupyStrategySlot( SQUAD_SLOT_SPECIAL_ATTACK ) )
		{
			return SCHED_PC_RANGE_ATTACK2;
		}
	}
#endif
	
	return SCHED_NONE;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CNPC_PlayerCompanion::CanReload( void )
{
	if ( IsRunningDynamicInteraction() )
		return false;

	return true;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CNPC_PlayerCompanion::ShouldDeferToFollowBehavior()
{
	if ( !GetFollowBehavior().CanSelectSchedule() || !GetFollowBehavior().FarFromFollowTarget() )
		return false;
		
	if ( m_StandoffBehavior.CanSelectSchedule() && !m_StandoffBehavior.IsBehindBattleLines( GetFollowBehavior().GetFollowTarget()->GetAbsOrigin() ) )
		return false;

	if ( HasCondition(COND_BETTER_WEAPON_AVAILABLE) && !GetActiveWeapon() )
	{
		// Unarmed allies should arm themselves as soon as the opportunity presents itself.
		return false;
	}

#if COMPANION_MELEE_ATTACK
	if (HasCondition(COND_CAN_MELEE_ATTACK1) /*&& !GetFollowBehavior().IsActive()*/)
	{
		// We should only get melee condition if we're not moving
		return false;
	}
#endif

	// Even though assault and act busy are placed ahead of the follow behavior in precedence, the below
	// code is necessary because we call ShouldDeferToFollowBehavior BEFORE we call the generic
	// BehaviorSelectSchedule, which tries the behaviors in priority order.
	if ( m_AssaultBehavior.CanSelectSchedule() && hl2_episodic.GetBool() )
	{
		return false;
	}

	if ( hl2_episodic.GetBool() )
	{
		if ( m_ActBusyBehavior.CanSelectSchedule() && m_ActBusyBehavior.IsCombatActBusy() )
		{
			return false;
		}
	}
	
	return true;
}

//-----------------------------------------------------------------------------
// CalcReasonableFacing() is asking us if there's any reason why we wouldn't
// want to look in this direction. 
//
// Right now this is used to help prevent citizens aiming their guns at each other
//-----------------------------------------------------------------------------
bool CNPC_PlayerCompanion::IsValidReasonableFacing( const Vector &vecSightDir, float sightDist )
{
	if( !GetActiveWeapon() )
	{
		// If I'm not armed, it doesn't matter if I'm looking at another citizen.
		return true;
	}

	if( ai_new_aiming.GetBool() )
	{
#ifdef MAPBASE
		// Hint node facing should still be obeyed
		if (GetHintNode() && GetHintNode()->GetIgnoreFacing() != HIF_YES)
			return true;
#endif

		Vector vecEyePositionCentered = GetAbsOrigin();
		vecEyePositionCentered.z = EyePosition().z;

		if( IsSquadmateInSpread(vecEyePositionCentered, vecEyePositionCentered + vecSightDir * 240.0f, VECTOR_CONE_15DEGREES.x, 12.0f * 3.0f) )
		{
			return false;
		}
	}

	return true;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
int CNPC_PlayerCompanion::TranslateSchedule( int scheduleType ) 
{
	switch( scheduleType )
	{
	case SCHED_IDLE_STAND:
	case SCHED_ALERT_STAND:
		if( GetActiveWeapon() )
		{
			// Everyone with less than half a clip takes turns reloading when not fighting.
			CBaseCombatWeapon *pWeapon = GetActiveWeapon();

			if( CanReload() && pWeapon->UsesClipsForAmmo1() && pWeapon->Clip1() < ( pWeapon->GetMaxClip1() * .5 ) && OccupyStrategySlot( SQUAD_SLOT_EXCLUSIVE_RELOAD ) )
			{
				if ( AI_IsSinglePlayer() )
				{
					CBasePlayer *pPlayer = UTIL_GetLocalPlayer();
					pWeapon = pPlayer->GetActiveWeapon();
					if( pWeapon && pWeapon->UsesClipsForAmmo1() && 
						pWeapon->Clip1() < ( pWeapon->GetMaxClip1() * .75 ) &&
						pPlayer->GetAmmoCount( pWeapon->GetPrimaryAmmoType() ) )
					{
#ifdef MAPBASE
						// Less annoying
						if ( !pWeapon->m_bInReload && (gpGlobals->curtime - GetLastEnemyTime()) > 5.0f )
#endif
						SpeakIfAllowed( TLK_PLRELOAD );
					}
				}
				return SCHED_RELOAD;
			}
		}
		break;

	case SCHED_COWER:
		return SCHED_PC_COWER;

	case SCHED_TAKE_COVER_FROM_BEST_SOUND:
		{
			CSound *pSound = GetBestSound(SOUND_DANGER);

			if( pSound && pSound->m_hOwner )
			{
				if( pSound->m_hOwner->IsNPC() && FClassnameIs( pSound->m_hOwner, "npc_zombine" ) )
				{
					// Run fully away from a Zombine with a grenade.
					return SCHED_PC_TAKE_COVER_FROM_BEST_SOUND;
				}
			}

			return SCHED_PC_MOVE_TOWARDS_COVER_FROM_BEST_SOUND;
		}

	case SCHED_FLEE_FROM_BEST_SOUND:
		return SCHED_PC_FLEE_FROM_BEST_SOUND;

	case SCHED_ESTABLISH_LINE_OF_FIRE:
#ifdef MAPBASE
		if ( CanAltFireEnemy(false) && OccupyStrategySlot(SQUAD_SLOT_SPECIAL_ATTACK) )
		{
			// If this companion has the balls to alt-fire the enemy's last known position,
			// do so!
			return SCHED_PC_AR2_ALTFIRE;
		}
#endif
	case SCHED_MOVE_TO_WEAPON_RANGE:
		if ( IsMortar( GetEnemy() ) )
			return SCHED_TAKE_COVER_FROM_ENEMY;
		break;

	case SCHED_CHASE_ENEMY:
		if ( IsMortar( GetEnemy() ) )
			return SCHED_TAKE_COVER_FROM_ENEMY;
#ifdef MAPBASE
		if ( GetEnemy() && EntIsClass( GetEnemy(), gm_isz_class_Gunship ) )
#else
		if ( GetEnemy() && FClassnameIs( GetEnemy(), "npc_combinegunship" ) )
#endif
			return SCHED_ESTABLISH_LINE_OF_FIRE;
		break;

	case SCHED_ESTABLISH_LINE_OF_FIRE_FALLBACK:
		// If we're fighting a gunship, try again
#ifdef MAPBASE
		if ( GetEnemy() && EntIsClass( GetEnemy(), gm_isz_class_Gunship ) )
#else
		if ( GetEnemy() && FClassnameIs( GetEnemy(), "npc_combinegunship" ) )
#endif
			return SCHED_ESTABLISH_LINE_OF_FIRE;
		break;

	case SCHED_RANGE_ATTACK1:
		if ( IsMortar( GetEnemy() ) )
			return SCHED_TAKE_COVER_FROM_ENEMY;
			
		if ( GetShotRegulator()->IsInRestInterval() )
			return SCHED_STANDOFF;

#ifdef MAPBASE
		if (CanAltFireEnemy( true ) && OccupyStrategySlot( SQUAD_SLOT_SPECIAL_ATTACK ))
		{
			// Since I'm holding this squadslot, no one else can try right now. If I die before the shot 
			// goes off, I won't have affected anyone else's ability to use this attack at their nearest
			// convenience.
			return SCHED_PC_AR2_ALTFIRE;
		}

		if ( !OccupyStrategySlotRange( SQUAD_SLOT_ATTACK1, SQUAD_SLOT_ATTACK2 ) )
		{
			// Throw a grenade if not allowed to engage with weapon.
			if ( CanGrenadeEnemy() )
			{
				if ( OccupyStrategySlot( SQUAD_SLOT_SPECIAL_ATTACK ) )
				{
					return SCHED_PC_RANGE_ATTACK2;
				}
			}

			return SCHED_STANDOFF;
		}
#else
		if( !OccupyStrategySlotRange( SQUAD_SLOT_ATTACK1, SQUAD_SLOT_ATTACK2 ) )
			return SCHED_STANDOFF;
#endif
		break;

#if COMPANION_MELEE_ATTACK
	//case SCHED_BACK_AWAY_FROM_ENEMY:
	//	if (HasCondition(COND_CAN_MELEE_ATTACK1))
	//		return SCHED_MELEE_ATTACK1;
	//	break;

	case SCHED_MELEE_ATTACK1:
		return SCHED_PC_MELEE_AND_MOVE_AWAY;
#endif

	case SCHED_FAIL_TAKE_COVER:
		if ( IsEnemyTurret() )
		{
			return SCHED_PC_FAIL_TAKE_COVER_TURRET;
		}
		break;
	case SCHED_RUN_FROM_ENEMY_FALLBACK:
		{
#if COMPANION_MELEE_ATTACK
			if (HasCondition(COND_CAN_MELEE_ATTACK1) && !HasCondition(COND_HEAVY_DAMAGE))
			{
				return SCHED_MELEE_ATTACK1;
			}
#endif
			if ( HasCondition( COND_CAN_RANGE_ATTACK1 ) )
			{
				return SCHED_RANGE_ATTACK1;
			}
			break;
		}

#ifdef MAPBASE
	case SCHED_TAKE_COVER_FROM_ENEMY:
		{
			if ( m_pSquad )
			{
				// Have to explicitly check innate range attack condition as may have weapon with range attack 2
				if ( HasCondition(COND_CAN_RANGE_ATTACK2)		&&
					OccupyStrategySlot( SQUAD_SLOT_SPECIAL_ATTACK ) )
				{
					SpeakIfAllowed("TLK_THROWGRENADE");
					return SCHED_PC_RANGE_ATTACK2;
				}
			}
		}
		break;
	case SCHED_HIDE_AND_RELOAD:
		{
			if( CanGrenadeEnemy() && OccupyStrategySlot( SQUAD_SLOT_SPECIAL_ATTACK ) && random->RandomInt( 0, 100 ) < 20 )
			{
				// If I COULD throw a grenade and I need to reload, 20% chance I'll throw a grenade before I hide to reload.
				return SCHED_PC_RANGE_ATTACK2;
			}
		}
		break;
#endif
	}

	return BaseClass::TranslateSchedule( scheduleType );
}

#ifdef MAPBASE
//extern float GetCurrentGravity( void );
#endif

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_PlayerCompanion::StartTask( const Task_t *pTask )
{
	switch( pTask->iTask )
	{
	case TASK_SOUND_WAKE:
		LocateEnemySound();
		SetWait( 0.5 );
		break;

	case TASK_ANNOUNCE_ATTACK:
		{
			if ( GetActiveWeapon() && m_AnnounceAttackTimer.Expired() )
			{
				if ( SpeakIfAllowed( TLK_ATTACKING, UTIL_VarArgs("attacking_with_weapon:%s", GetActiveWeapon()->GetClassname()) ) )
				{
					m_AnnounceAttackTimer.Set( 10, 30 );
				}
			}

			BaseClass::StartTask( pTask );
			break;
		}

	case TASK_PC_WAITOUT_MORTAR:
		if ( HasCondition( COND_NO_HEAR_DANGER ) )
			TaskComplete();
		break;

	case TASK_MOVE_AWAY_PATH:
		{
			if ( m_bMovingAwayFromPlayer )
				SpeakIfAllowed( TLK_PLPUSH );

			BaseClass::StartTask( pTask );
		}
		break;

	case TASK_PC_GET_PATH_OFF_COMPANION:
		{
			Assert( ( GetGroundEntity() && ( GetGroundEntity()->IsPlayer() || ( GetGroundEntity()->IsNPC() && IRelationType( GetGroundEntity() ) == D_LI ) ) ) );
			GetNavigator()->SetAllowBigStep( GetGroundEntity() );
			ChainStartTask( TASK_MOVE_AWAY_PATH, 48 );
			
			/*
			trace_t tr;
			UTIL_TraceHull( GetAbsOrigin(), GetAbsOrigin(), GetHullMins(), GetHullMaxs(), MASK_NPCSOLID, this, COLLISION_GROUP_NONE, &tr );
			if ( tr.startsolid && tr.m_pEnt == GetGroundEntity() )
			{
				// Allow us to move through the entity for a short time
				NPCPhysics_CreateSolver( this, GetGroundEntity(), true, 2.0f );
			}
			*/
		}
		break;

#ifdef MAPBASE
	case TASK_PC_PLAY_SEQUENCE_FACE_ALTFIRE_TARGET:
		StartTask_FaceAltFireTarget( pTask );
		break;

	case TASK_PC_GET_PATH_TO_FORCED_GREN_LOS:
		StartTask_GetPathToForced( pTask );
		break;

	case TASK_PC_DEFER_SQUAD_GRENADES:
		StartTask_DeferSquad( pTask );
		break;

	case TASK_PC_FACE_TOSS_DIR:
		break;
#endif

	default:
		BaseClass::StartTask( pTask );
		break;
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_PlayerCompanion::RunTask( const Task_t *pTask )
{
	switch( pTask->iTask )
	{
		case TASK_SOUND_WAKE:
			if( IsWaitFinished() )
			{
				TaskComplete();
			}
			break;

		case TASK_PC_WAITOUT_MORTAR:
			{
				if ( HasCondition( COND_NO_HEAR_DANGER ) )
					TaskComplete();
			}
			break;

		case TASK_MOVE_AWAY_PATH:
			{
				BaseClass::RunTask( pTask );

				if ( GetNavigator()->IsGoalActive() && !GetEnemy() )
				{
					AddFacingTarget( EyePosition() + BodyDirection2D() * 240, 1.0, 2.0 );
				}
			}
			break;

		case TASK_PC_GET_PATH_OFF_COMPANION:
			{
				if ( AI_IsSinglePlayer() )
				{
					GetNavigator()->SetAllowBigStep( UTIL_GetLocalPlayer() );
				}
				ChainRunTask( TASK_MOVE_AWAY_PATH, 48 );
			}
			break;

#ifdef MAPBASE
		case TASK_PC_PLAY_SEQUENCE_FACE_ALTFIRE_TARGET:
			RunTask_FaceAltFireTarget( pTask );
			break;

		case TASK_PC_GET_PATH_TO_FORCED_GREN_LOS:
			RunTask_GetPathToForced( pTask );
			break;

		case TASK_PC_FACE_TOSS_DIR:
			RunTask_FaceTossDir( pTask );
			break;
#endif

		default:
			BaseClass::RunTask( pTask );
			break;
	}
}

//-----------------------------------------------------------------------------
// Parses this NPC's activity remap from the actremap.txt file
//-----------------------------------------------------------------------------
void CNPC_PlayerCompanion::PrepareReadinessRemap( void )
{
	CUtlVector< CActivityRemap > entries;
	UTIL_LoadActivityRemapFile( "scripts/actremap.txt", "npc_playercompanion", entries );

	for ( int i = 0; i < entries.Count(); i++ )
	{
		CCompanionActivityRemap ActRemap;
		Q_memcpy( &ActRemap, &entries[i], sizeof( CActivityRemap ) );

		KeyValues *pExtraBlock = ActRemap.GetExtraKeyValueBlock();

		if ( pExtraBlock )
		{
			KeyValues *pKey = pExtraBlock->GetFirstSubKey();

			while ( pKey )
			{
				const char *pKeyName = pKey->GetName();
				const char *pKeyValue = pKey->GetString();

				if ( !stricmp( pKeyName, "readiness" ) )
				{
					ActRemap.m_fUsageBits |= bits_REMAP_READINESS;

					if ( !stricmp( pKeyValue, "AIRL_PANIC" ) )
					{
						ActRemap.m_readiness = AIRL_PANIC;
					}
					else if ( !stricmp( pKeyValue, "AIRL_STEALTH" ) )
					{
						ActRemap.m_readiness = AIRL_STEALTH;
					}
					else if ( !stricmp( pKeyValue, "AIRL_RELAXED" ) )
					{
						ActRemap.m_readiness = AIRL_RELAXED;
					}
					else if ( !stricmp( pKeyValue, "AIRL_STIMULATED" ) )
					{
						ActRemap.m_readiness = AIRL_STIMULATED;
					}
					else if ( !stricmp( pKeyValue, "AIRL_AGITATED" ) )
					{
						ActRemap.m_readiness = AIRL_AGITATED;
					}
				}
				else if ( !stricmp( pKeyName, "aiming" ) )
				{
					ActRemap.m_fUsageBits |= bits_REMAP_AIMING;

					if ( !stricmp( pKeyValue, "TRS_NONE" ) )
					{
						// This is the new way to say that we don't care, the tri-state was abandoned (jdw)
						ActRemap.m_fUsageBits &= ~bits_REMAP_AIMING;
					}
					else if ( !stricmp( pKeyValue, "TRS_FALSE" ) || !stricmp( pKeyValue, "FALSE" ) )
					{
						ActRemap.m_bAiming = false;
					}
					else if ( !stricmp( pKeyValue, "TRS_TRUE" ) || !stricmp( pKeyValue, "TRUE" ) )
					{
						ActRemap.m_bAiming = true;
					}
				} 
				else if ( !stricmp( pKeyName, "weaponrequired" ) )
				{
					ActRemap.m_fUsageBits |= bits_REMAP_WEAPON_REQUIRED;

					if ( !stricmp( pKeyValue, "TRUE" ) )
					{
						ActRemap.m_bWeaponRequired = true;
					}
					else if ( !stricmp( pKeyValue, "FALSE" ) )
					{
						ActRemap.m_bWeaponRequired = false;
					}
				}
				else if ( !stricmp( pKeyName, "invehicle" ) )
				{
					ActRemap.m_fUsageBits |= bits_REMAP_IN_VEHICLE;

					if ( !stricmp( pKeyValue, "TRUE" ) )
					{
						ActRemap.m_bInVehicle = true;
					}
					else if ( !stricmp( pKeyValue, "FALSE" ) )
					{
						ActRemap.m_bInVehicle = false;
					}
				}

				pKey = pKey->GetNextKey();
			}
		}

		const char *pActName = ActivityList_NameForIndex( (int)ActRemap.mappedActivity );

		if ( GetActivityID( pActName ) == ACT_INVALID )
		{
			AddActivityToSR( pActName, (int)ActRemap.mappedActivity );
		}

		m_activityMappings.AddToTail( ActRemap );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_PlayerCompanion::Activate( void )
{
	BaseClass::Activate();

	PrepareReadinessRemap();
}

//-----------------------------------------------------------------------------
// Purpose: Translate an activity given a list of criteria
//-----------------------------------------------------------------------------
Activity CNPC_PlayerCompanion::TranslateActivityReadiness( Activity activity )
{
	// If we're in an actbusy, we don't want to mess with this
	if ( m_ActBusyBehavior.IsActive() )
		return activity;

	if ( m_bReadinessCapable && 
		 ( GetReadinessUse() == AIRU_ALWAYS || 
		   ( GetReadinessUse() == AIRU_ONLY_PLAYER_SQUADMATES && (IsInPlayerSquad()||Classify()==CLASS_PLAYER_ALLY_VITAL) ) ) )
	{
		bool bShouldAim = ShouldBeAiming();

		for ( int i = 0; i < m_activityMappings.Count(); i++ )
		{
			// Get our activity remap
			CCompanionActivityRemap actremap = m_activityMappings[i];

			// Activity must match
			if ( activity != actremap.activity )
				continue;

			// Readiness must match
			if ( ( actremap.m_fUsageBits & bits_REMAP_READINESS ) && GetReadinessLevel() != actremap.m_readiness )
				continue;

			// Deal with weapon state
			if ( ( actremap.m_fUsageBits & bits_REMAP_WEAPON_REQUIRED ) && actremap.m_bWeaponRequired )
			{
				// Must have a weapon
				if ( GetActiveWeapon() == NULL )
					continue;
				
				// Must either not care about aiming, or agree on aiming
				if ( actremap.m_fUsageBits & bits_REMAP_AIMING )
				{
					if ( bShouldAim && actremap.m_bAiming == false )
						continue;

					if ( bShouldAim == false && actremap.m_bAiming )
						continue;
				}
			}

			// Must care about vehicle status
			if ( actremap.m_fUsageBits & bits_REMAP_IN_VEHICLE )
			{
				// Deal with the two vehicle states
				if ( actremap.m_bInVehicle && IsInAVehicle() == false )
					continue;

				if ( actremap.m_bInVehicle == false && IsInAVehicle() )
					continue;
			}

			// We've successfully passed all criteria for remapping this 
			return actremap.mappedActivity;
		}
	}

	return activity;
}


//-----------------------------------------------------------------------------
// Purpose: Override base class activiites
//-----------------------------------------------------------------------------
Activity CNPC_PlayerCompanion::NPC_TranslateActivity( Activity activity )
{
	if ( activity == ACT_COWER )
		return ACT_COVER_LOW;

	if ( activity == ACT_RUN && ( IsCurSchedule( SCHED_TAKE_COVER_FROM_BEST_SOUND ) || IsCurSchedule( SCHED_FLEE_FROM_BEST_SOUND ) ) )
	{
		if ( random->RandomInt( 0, 1 ) && HaveSequenceForActivity( ACT_RUN_PROTECTED ) )
			activity = ACT_RUN_PROTECTED;
	}

#ifdef COMPANION_HOLSTER_WORKAROUND
	if (activity == ACT_DISARM || activity == ACT_ARM)
	{
		CBaseCombatWeapon *pWeapon = GetActiveWeapon() ? GetActiveWeapon() : m_hWeapons[m_iLastHolsteredWeapon];
		if (pWeapon && pWeapon->WeaponClassify() != WEPCLASS_HANDGUN)
		{
			switch (activity)
			{
			case ACT_DISARM:	return ACT_DISARM_RIFLE;
			case ACT_ARM:		return ACT_ARM_RIFLE;
			}
		}
	}
#endif

	activity = BaseClass::NPC_TranslateActivity( activity );

	if ( activity == ACT_IDLE  )
	{
		if ( (m_NPCState == NPC_STATE_COMBAT || m_NPCState == NPC_STATE_ALERT) && gpGlobals->curtime - m_flLastAttackTime < 3)
		{
			activity = ACT_IDLE_ANGRY;
		}
	}

#ifdef MAPBASE
	// Vorts use ACT_RANGE_ATTACK2, but they should translate to ACT_VORTIGAUNT_DISPEL
	// before that reaches this code...
	if (activity == ACT_RANGE_ATTACK2)
	{
		activity = ACT_COMBINE_THROW_GRENADE;
	}
#endif

	return TranslateActivityReadiness( activity );
}

//------------------------------------------------------------------------------
// Purpose: Handle animation events
//------------------------------------------------------------------------------
void CNPC_PlayerCompanion::HandleAnimEvent( animevent_t *pEvent )
{
#ifdef HL2_EPISODIC
	// Create a flare and parent to our hand
	if ( pEvent->event == AE_COMPANION_PRODUCE_FLARE )
	{
		m_hFlare = static_cast<CPhysicsProp *>(CreateEntityByName( "prop_physics" ));
		if ( m_hFlare != NULL )
		{
			// Set the model
			m_hFlare->SetModel( "models/props_junk/flare.mdl" );
			
			// Set the parent attachment
			m_hFlare->SetParent( this );
			m_hFlare->SetParentAttachment( "SetParentAttachment", pEvent->options, false );
		}

		return;
	}

	// Start the flare up with proper fanfare
	if ( pEvent->event == AE_COMPANION_LIGHT_FLARE )
	{
		if ( m_hFlare != NULL )
		{
			m_hFlare->CreateFlare( 5*60.0f );
		}
		
		return;
	}

	// Drop the flare to the ground
	if ( pEvent->event == AE_COMPANION_RELEASE_FLARE )
	{
		// Detach
		m_hFlare->SetParent( NULL );
		m_hFlare->Spawn();
		m_hFlare->RemoveInteraction( PROPINTER_PHYSGUN_CREATE_FLARE );

		// Disable collisions between the NPC and the flare
		PhysDisableEntityCollisions( this, m_hFlare );

		// TODO: Find the velocity of the attachment point, at this time, in the animation cycle

		// Construct a toss velocity
		Vector vecToss;
		AngleVectors( GetAbsAngles(), &vecToss );
		VectorNormalize( vecToss );
		vecToss *= random->RandomFloat( 64.0f, 72.0f );
		vecToss[2] += 64.0f;

		// Throw it
		IPhysicsObject *pObj = m_hFlare->VPhysicsGetObject();
		pObj->ApplyForceCenter( vecToss );

		// Forget about the flare at this point
		m_hFlare = NULL;

		return;
	}
#endif // HL2_EPISODIC

	switch( pEvent->event )
	{
	case EVENT_WEAPON_RELOAD:
		if ( GetActiveWeapon() )
		{
#ifdef MAPBASE
			GetActiveWeapon()->Reload_NPC();
#else
			GetActiveWeapon()->WeaponSound( RELOAD_NPC );
			GetActiveWeapon()->m_iClip1 = GetActiveWeapon()->GetMaxClip1(); 
#endif
			ClearCondition(COND_LOW_PRIMARY_AMMO);
			ClearCondition(COND_NO_PRIMARY_AMMO);
			ClearCondition(COND_NO_SECONDARY_AMMO);
		}
		break;

#if COMPANION_MELEE_ATTACK
	case AE_PC_MELEE:
		{
			CBaseEntity *pHurt = CheckTraceHullAttack(COMPANION_MELEE_DIST, -Vector(16, 16, 18), Vector(16, 16, 18), 0, DMG_CLUB);
			CBaseCombatCharacter* pBCC = ToBaseCombatCharacter(pHurt);
			if (pBCC)
			{
				Vector forward, up;
				AngleVectors(GetLocalAngles(), &forward, NULL, &up);

				if (pBCC->IsPlayer())
				{
					pBCC->ViewPunch(QAngle(-12, -7, 0));
					pHurt->ApplyAbsVelocityImpulse(forward * 100 + up * 50);
				}

				CTakeDamageInfo info(this, this, m_nMeleeDamage, DMG_CLUB);
				CalculateMeleeDamageForce(&info, forward, pBCC->GetAbsOrigin());
				pBCC->TakeDamage(info);

				EmitSound("NPC_Combine.WeaponBash");
			}
			break;
		}
#endif

	default:
		BaseClass::HandleAnimEvent( pEvent );
		break;
	}
}

//-----------------------------------------------------------------------------
// Purpose:  This is a generic function (to be implemented by sub-classes) to
//			 handle specific interactions between different types of characters
//			 (For example the barnacle grabbing an NPC)
// Input  :  Constant for the type of interaction
// Output :	 true  - if sub-class has a response for the interaction
//			 false - if sub-class has no response
//-----------------------------------------------------------------------------
bool CNPC_PlayerCompanion::HandleInteraction(int interactionType, void *data, CBaseCombatCharacter* sourceEnt)
{
	if (interactionType == g_interactionHitByPlayerThrownPhysObj )
	{
		if ( IsOkToSpeakInResponseToPlayer() )
		{
			Speak( TLK_PLYR_PHYSATK );
		}
		return true;
	}

	return BaseClass::HandleInteraction( interactionType, data, sourceEnt );
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
int CNPC_PlayerCompanion::GetSoundInterests()
{
	return	SOUND_WORLD				|
			SOUND_COMBAT			|
			SOUND_PLAYER			|
			SOUND_DANGER			|
			SOUND_BULLET_IMPACT		|
			SOUND_MOVE_AWAY			|
			SOUND_READINESS_LOW		|
			SOUND_READINESS_MEDIUM	|
			SOUND_READINESS_HIGH;
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
void CNPC_PlayerCompanion::Touch( CBaseEntity *pOther )
{
	BaseClass::Touch( pOther );

	// Did the player touch me?
	if ( pOther->IsPlayer() || ( pOther->VPhysicsGetObject() && (pOther->VPhysicsGetObject()->GetGameFlags() & FVPHYSICS_PLAYER_HELD ) ) )
	{
		// Ignore if pissed at player
		if ( m_afMemory & bits_MEMORY_PROVOKED )
			return;
			
		TestPlayerPushing( ( pOther->IsPlayer() ) ? pOther : AI_GetSinglePlayer() );
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_PlayerCompanion::ModifyOrAppendCriteria( AI_CriteriaSet& set )
{
	BaseClass::ModifyOrAppendCriteria( set );
	if ( GetEnemy() && IsMortar( GetEnemy() ) )
	{
		set.RemoveCriteria( "enemy" );
		set.AppendCriteria( "enemy", STRING(gm_iszMortarClassname) );
	}

	if ( HasCondition( COND_PC_HURTBYFIRE ) )
	{
		set.AppendCriteria( "hurt_by_fire", "1" );
	}

#ifdef MAPBASE
	// Ported from Alyx.
	AIEnemiesIter_t iter;
	int iNumEnemies = 0;
	for ( AI_EnemyInfo_t *pEMemory = GetEnemies()->GetFirst(&iter); pEMemory != NULL; pEMemory = GetEnemies()->GetNext(&iter) )
	{
		if ( pEMemory->hEnemy->IsAlive() && ( pEMemory->hEnemy->Classify() != CLASS_BULLSEYE ) )
		{
			iNumEnemies++;
		}
	}
	set.AppendCriteria( "num_enemies", UTIL_VarArgs( "%d", iNumEnemies ) );
#endif

	if ( m_bReadinessCapable )
	{
		switch( GetReadinessLevel() )
		{
		case AIRL_PANIC:
			set.AppendCriteria( "readiness", "panic" );
			break;

		case AIRL_STEALTH:
			set.AppendCriteria( "readiness", "stealth" );
			break;

		case AIRL_RELAXED:
			set.AppendCriteria( "readiness", "relaxed" );
			break;

		case AIRL_STIMULATED:
			set.AppendCriteria( "readiness", "stimulated" );
			break;

		case AIRL_AGITATED:
			set.AppendCriteria( "readiness", "agitated" );
			break;
		}
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CNPC_PlayerCompanion::IsReadinessCapable()
{
	if ( GlobalEntity_GetState("gordon_precriminal") == GLOBAL_ON )
		return false;

#ifndef HL2_EPISODIC
	// Allow episodic companions to use readiness even if unarmed. This allows for the panicked 
	// citizens in ep1_c17_05 (sjb)
	if( !GetActiveWeapon() )
		return false;
#endif

#ifdef MAPBASE
#ifdef HL2_EPISODIC
	if (GetActiveWeapon())
#else
	// We already know we have a weapon due to the check above
#endif
	{
		// Rather than looking up the activity string, we just make sure our weapon accepts a few basic readiness activity overrides.
		// This lets us make sure our weapon is readiness-capable to begin with.
		CBaseCombatWeapon *pWeapon = GetActiveWeapon();
		if ( pWeapon->ActivityOverride(ACT_IDLE_RELAXED, NULL) == ACT_IDLE_RELAXED &&
			pWeapon->ActivityOverride( ACT_IDLE_STIMULATED, NULL ) == ACT_IDLE_STIMULATED &&
			pWeapon->ActivityOverride( ACT_IDLE_AGITATED, NULL ) == ACT_IDLE_AGITATED )
			return false;

		if (LookupActivity( "ACT_IDLE_AIM_RIFLE_STIMULATED" ) == ACT_INVALID)
			return false;

		if (EntIsClass(GetActiveWeapon(), gm_isz_class_RPG))
			return false;
	}
#else
	if( GetActiveWeapon() && LookupActivity("ACT_IDLE_AIM_RIFLE_STIMULATED") == ACT_INVALID )
		return false;

	if( GetActiveWeapon() && FClassnameIs( GetActiveWeapon(), "weapon_rpg" ) )
		return false;
#endif

	return true;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_PlayerCompanion::AddReadiness( float flAdd, bool bOverrideLock )
{
	if( IsReadinessLocked() && !bOverrideLock )
		return;

	SetReadinessValue( GetReadinessValue() + flAdd );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_PlayerCompanion::SubtractReadiness( float flSub, bool bOverrideLock )
{
 	if( IsReadinessLocked() && !bOverrideLock )
		return;

	// Prevent readiness from going below 0 (below 0 is only for scripted states)
	SetReadinessValue( MAX(GetReadinessValue() - flSub, 0) );
}

//-----------------------------------------------------------------------------
// This method returns false if the NPC is not allowed to change readiness at this point.
//-----------------------------------------------------------------------------
bool CNPC_PlayerCompanion::AllowReadinessValueChange( void )
{
	if ( GetIdealActivity() == ACT_IDLE || GetIdealActivity() == ACT_WALK || GetIdealActivity() == ACT_RUN )
		return true;

	if ( HasActiveLayer() == true )
		return false;

	return false;
}

//-----------------------------------------------------------------------------
// NOTE: This function ignores the lock. Use the interface functions.
//-----------------------------------------------------------------------------
void CNPC_PlayerCompanion::SetReadinessValue( float flSet )
{
	if ( AllowReadinessValueChange() == false )
		return;

	int priorReadiness = GetReadinessLevel();

	flSet = MIN( 1.0f, flSet );
	flSet = MAX( READINESS_MIN_VALUE, flSet );

	m_flReadiness = flSet;

	if( GetReadinessLevel() != priorReadiness )
	{
		// We've been bumped up into a different readiness level.
		// Interrupt IDLE schedules (if we're playing one) so that 
		// we can pick the proper animation.
		SetCondition( COND_IDLE_INTERRUPT );

		// Force us to recalculate our animation. If we don't do this,
		// our translated activity may change, but not our root activity,
		// and then we won't actually visually change anims.
		ResetActivity();

		//Force the NPC to recalculate it's arrival sequence since it'll most likely be wrong now that we changed readiness level.
		GetNavigator()->SetArrivalSequence( ACT_INVALID );

		ReadinessLevelChanged( priorReadiness );
	}
}

//-----------------------------------------------------------------------------
// if bOverrideLock, you'll change the readiness level even if we're within
// a time period during which someone else has locked the level.
//
// if bSlam, you'll allow the readiness level to be set lower than current. 
//-----------------------------------------------------------------------------
void CNPC_PlayerCompanion::SetReadinessLevel( int iLevel, bool bOverrideLock, bool bSlam )
{
	if( IsReadinessLocked() && !bOverrideLock )
		return;

	switch( iLevel )
	{
	case AIRL_PANIC:
		if( bSlam )
			SetReadinessValue( READINESS_MODE_PANIC );
		break;
	case AIRL_STEALTH:
		if( bSlam )
			SetReadinessValue( READINESS_MODE_STEALTH );
		break;
	case AIRL_RELAXED:
		if( bSlam || GetReadinessValue() < READINESS_VALUE_RELAXED )
			SetReadinessValue( READINESS_VALUE_RELAXED );
		break;
	case AIRL_STIMULATED:
		if( bSlam || GetReadinessValue() < READINESS_VALUE_STIMULATED )
			SetReadinessValue( READINESS_VALUE_STIMULATED );
		break;
	case AIRL_AGITATED:
		if( bSlam || GetReadinessValue() < READINESS_VALUE_AGITATED )
			SetReadinessValue( READINESS_VALUE_AGITATED );
		break;
	default:
		DevMsg("ERROR: Bad readiness level\n");
		break;
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
int	CNPC_PlayerCompanion::GetReadinessLevel()
{
	if ( m_bReadinessCapable == false )
		return AIRL_RELAXED;

	if( m_flReadiness == READINESS_MODE_PANIC )
	{
		return AIRL_PANIC;
	}

	if( m_flReadiness == READINESS_MODE_STEALTH )
	{
		return AIRL_STEALTH;
	}

	if( m_flReadiness <= READINESS_VALUE_RELAXED )
	{
		return AIRL_RELAXED;
	}

	if( m_flReadiness <= READINESS_VALUE_STIMULATED )
	{
		return AIRL_STIMULATED;
	}

	return AIRL_AGITATED;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_PlayerCompanion::UpdateReadiness()
{
	// Only update readiness if it's not in a scripted state
	if ( !IsInScriptedReadinessState() )
	{
		if( HasCondition(COND_HEAR_COMBAT) || HasCondition(COND_HEAR_BULLET_IMPACT)	)
			SetReadinessLevel( AIRL_STIMULATED, false, false );

		if( HasCondition(COND_HEAR_DANGER) || HasCondition(COND_SEE_ENEMY) )
			SetReadinessLevel( AIRL_AGITATED, false, false );

		if( m_flReadiness > 0.0f && GetReadinessDecay() > 0 )
		{
			// Decay.
			SubtractReadiness( ( 0.1 * (1.0f/GetReadinessDecay())) * m_flReadinessSensitivity );
		}
	}

 	if( ai_debug_readiness.GetBool() && AI_IsSinglePlayer() )
	{
		// Draw the readiness-o-meter
		Vector vecSpot;
		Vector vecOffset( 0, 0, 12 );
		const float BARLENGTH = 12.0f;
		const float GRADLENGTH	= 4.0f;

		Vector right;
		UTIL_PlayerByIndex( 1 )->GetVectors( NULL, &right, NULL );

		if ( IsInScriptedReadinessState() )
 		{
			// Just print the name of the scripted state
			vecSpot = EyePosition() + vecOffset;

			if( GetReadinessLevel() == AIRL_STEALTH )
			{
				NDebugOverlay::Text( vecSpot, "Stealth", true, 0.1 );
			}
			else if( GetReadinessLevel() == AIRL_PANIC )
			{
				NDebugOverlay::Text( vecSpot, "Panic", true, 0.1 );
			}
			else
			{
				NDebugOverlay::Text( vecSpot, "Unspecified", true, 0.1 );
			}
		}
		else
		{
			vecSpot = EyePosition() + vecOffset;
			NDebugOverlay::Line( vecSpot, vecSpot + right * GRADLENGTH, 255, 255, 255, false, 0.1 );

			vecSpot = EyePosition() + vecOffset + Vector( 0, 0, BARLENGTH * READINESS_VALUE_RELAXED );
			NDebugOverlay::Line( vecSpot, vecSpot + right * GRADLENGTH, 0, 255, 0, false, 0.1 );

			vecSpot = EyePosition() + vecOffset + Vector( 0, 0, BARLENGTH * READINESS_VALUE_STIMULATED );
			NDebugOverlay::Line( vecSpot, vecSpot + right * GRADLENGTH, 255, 255, 0, false, 0.1 );

			vecSpot = EyePosition() + vecOffset + Vector( 0, 0, BARLENGTH * READINESS_VALUE_AGITATED );
			NDebugOverlay::Line( vecSpot, vecSpot + right * GRADLENGTH, 255, 0, 0, false, 0.1 );

			vecSpot = EyePosition() + vecOffset;
			NDebugOverlay::Line( vecSpot, vecSpot + Vector( 0, 0, BARLENGTH * GetReadinessValue() ), 255, 255, 0, false, 0.1 );
		}
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
float CNPC_PlayerCompanion::GetReadinessDecay()
{
	return ai_readiness_decay.GetFloat();
}

//-----------------------------------------------------------------------------
// Passing NULL to clear the aim target is acceptible.
//-----------------------------------------------------------------------------
void CNPC_PlayerCompanion::SetAimTarget( CBaseEntity *pTarget )
{
	if( pTarget != NULL && IsAllowedToAim() )
	{
		m_hAimTarget = pTarget;
	}
	else
	{
		m_hAimTarget = NULL;
	}

	Activity NewActivity = NPC_TranslateActivity(GetActivity());

	//Don't set the ideal activity to an activity that might not be there.
	if ( SelectWeightedSequence( NewActivity ) == ACT_INVALID )
		 return;

	if (NewActivity != GetActivity() )
	{
		SetIdealActivity( NewActivity );
	}

#if 0
	if( m_hAimTarget )
	{
		Msg("New Aim Target: %s\n", m_hAimTarget->GetClassname() );
		NDebugOverlay::Line(EyePosition(), m_hAimTarget->WorldSpaceCenter(), 255, 255, 0, false, 0.1 );
	}
#endif
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_PlayerCompanion::StopAiming( char *pszReason )
{
#if 0
	if( pszReason )
	{	
		Msg("Stopped aiming because %s\n", pszReason );
	}
#endif

	SetAimTarget(NULL);

	Activity NewActivity = NPC_TranslateActivity(GetActivity());
	if (NewActivity != GetActivity())
	{
		SetIdealActivity( NewActivity );
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
#define COMPANION_MAX_LOOK_TIME	3.0f
#define COMPANION_MIN_LOOK_TIME	1.0f
#define COMPANION_MAX_TACTICAL_TARGET_DIST	1800.0f // 150 feet

bool CNPC_PlayerCompanion::PickTacticalLookTarget( AILookTargetArgs_t *pArgs )
{
	if( HasCondition( COND_SEE_ENEMY ) )
	{
		// Don't bother. We're dealing with our enemy.
		return false;
	}

	float flMinLookTime;
	float flMaxLookTime;

	// Excited companions will look at each target only briefly and then find something else to look at.
	flMinLookTime = COMPANION_MIN_LOOK_TIME + ((COMPANION_MAX_LOOK_TIME-COMPANION_MIN_LOOK_TIME) * (1.0f - GetReadinessValue()) );

	switch( GetReadinessLevel() )
	{
	case AIRL_RELAXED:
		// Linger on targets, look at them for quite a while.
		flMinLookTime = COMPANION_MAX_LOOK_TIME + random->RandomFloat( 0.0f, 2.0f );
		break;

	case AIRL_STIMULATED:
		// Look around a little quicker.
		flMinLookTime = COMPANION_MIN_LOOK_TIME + random->RandomFloat( 0.0f, COMPANION_MAX_LOOK_TIME - 1.0f );
		break;

	case AIRL_AGITATED:
		// Look around very quickly
		flMinLookTime = COMPANION_MIN_LOOK_TIME;
		break;
	}

	flMaxLookTime = flMinLookTime + random->RandomFloat( 0.0f, 0.5f );
	pArgs->flDuration = random->RandomFloat( flMinLookTime, flMaxLookTime );

	if( HasCondition(COND_SEE_PLAYER) && hl2_episodic.GetBool() )
	{
		// 1/3rd chance to authoritatively look at player
		if( random->RandomInt( 0, 2 ) == 0 )
		{
			pArgs->hTarget = AI_GetSinglePlayer();
			return true;
		}
	}

	// Use hint nodes
	CAI_Hint *pHint;
	CHintCriteria hintCriteria;

	hintCriteria.AddHintType( HINT_WORLD_VISUALLY_INTERESTING );
	hintCriteria.AddHintType( HINT_WORLD_VISUALLY_INTERESTING_DONT_AIM );
	hintCriteria.AddHintType( HINT_WORLD_VISUALLY_INTERESTING_STEALTH );
	hintCriteria.SetFlag( bits_HINT_NODE_VISIBLE | bits_HINT_NODE_IN_VIEWCONE | bits_HINT_NPC_IN_NODE_FOV );
	hintCriteria.AddIncludePosition( GetAbsOrigin(), COMPANION_MAX_TACTICAL_TARGET_DIST );

	{
		AI_PROFILE_SCOPE( CNPC_PlayerCompanion_FindHint_PickTacticalLookTarget );
  		pHint = CAI_HintManager::FindHint( this, hintCriteria );
	}
	
	if( pHint )
	{
		pArgs->hTarget = pHint;
		
		// Turn this node off for a few seconds to stop others aiming at the same thing (except for stealth nodes)
		if ( pHint->HintType() != HINT_WORLD_VISUALLY_INTERESTING_STEALTH )
		{
			pHint->DisableForSeconds( 5.0f );
		}
		return true;
	}

	// See what the base class thinks.
	return BaseClass::PickTacticalLookTarget( pArgs );
}

//-----------------------------------------------------------------------------
// Returns true if changing target.
//-----------------------------------------------------------------------------
bool CNPC_PlayerCompanion::FindNewAimTarget()
{
	if( GetEnemy() )
	{
		// Don't bother. Aim at enemy.
		return false;
	}

	if( !m_bReadinessCapable || GetReadinessLevel() == AIRL_RELAXED )
	{
		// If I'm relaxed (don't want to aim), or physically incapable,
		// don't run this hint node searching code.
		return false;
	}

	CAI_Hint *pHint;
	CHintCriteria hintCriteria;
	CBaseEntity *pPriorAimTarget = GetAimTarget();

	hintCriteria.SetHintType( HINT_WORLD_VISUALLY_INTERESTING );
	hintCriteria.SetFlag( bits_HINT_NODE_VISIBLE | bits_HINT_NODE_IN_VIEWCONE | bits_HINT_NPC_IN_NODE_FOV );
	hintCriteria.AddIncludePosition( GetAbsOrigin(), COMPANION_MAX_TACTICAL_TARGET_DIST );
	pHint = CAI_HintManager::FindHint( this, hintCriteria );

	if( pHint )
	{
		if( (pHint->GetAbsOrigin() - GetAbsOrigin()).Length2D() < COMPANION_AIMTARGET_NEAREST )
		{
			// Too close!
			return false;
		}

		if( !HasAimLOS(pHint) )
		{
			// No LOS
			return false;
		}

		if( pHint != pPriorAimTarget )
		{
			// Notify of the change.
			SetAimTarget( pHint );
			return true;
		}
	}

	// Didn't find an aim target, or found the same one.
	return false;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_PlayerCompanion::OnNewLookTarget()
{
	if( ai_new_aiming.GetBool() )
	{
		if( GetLooktarget() )
		{
			// See if our looktarget is a reasonable aim target.
			CAI_Hint *pHint = dynamic_cast<CAI_Hint*>( GetLooktarget() );

			if( pHint )
			{
				if( pHint->HintType() == HINT_WORLD_VISUALLY_INTERESTING &&
					(pHint->GetAbsOrigin() - GetAbsOrigin()).Length2D() > COMPANION_AIMTARGET_NEAREST  &&
					FInAimCone(pHint->GetAbsOrigin())	&&
					HasAimLOS(pHint) )
				{
					SetAimTarget( pHint );
					return;
				}
			}
		}

		// Search for something else.
		FindNewAimTarget();
	}
	else
	{
		if( GetLooktarget() )
		{
			// Have picked a new entity to look at. Should we copy it to the aim target?
			if( IRelationType( GetLooktarget() ) == D_LI )
			{
				// Don't aim at friends, just keep the old target (if any)
				return;
			}

			if( (GetLooktarget()->GetAbsOrigin() - GetAbsOrigin()).Length2D() < COMPANION_AIMTARGET_NEAREST )
			{
				// Too close!
				return;
			}

			if( !HasAimLOS( GetLooktarget() ) )
			{
				// No LOS
				return;
			}

			SetAimTarget( GetLooktarget() );
		}
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CNPC_PlayerCompanion::ShouldBeAiming() 
{
	if( !IsAllowedToAim() )
	{
		return false;
	}

	if( !GetEnemy() && !GetAimTarget() )
	{
		return false;
	}

	if( GetEnemy() && !HasCondition(COND_SEE_ENEMY) )
	{
		return false;
	}

	return true;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
#define PC_MAX_ALLOWED_AIM	2
bool CNPC_PlayerCompanion::IsAllowedToAim()
{
	if( !m_pSquad )
		return true;

	if( GetReadinessLevel() == AIRL_AGITATED )
	{
		// Agitated companions can always aim. This makes the squad look
		// more alert as a whole when something very serious/dangerous has happened.
		return true;
	}

	int count = 0;
	
	// If I'm in a squad, only a certain number of us can aim.
	AISquadIter_t iter;
	for ( CAI_BaseNPC *pSquadmate = m_pSquad->GetFirstMember(&iter); pSquadmate; pSquadmate = m_pSquad->GetNextMember(&iter) )
	{
		CNPC_PlayerCompanion *pCompanion = dynamic_cast<CNPC_PlayerCompanion*>(pSquadmate);
		if( pCompanion && pCompanion != this && pCompanion->GetAimTarget() != NULL )
		{
			count++;
		}
	}

	if( count < PC_MAX_ALLOWED_AIM )
	{
		return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CNPC_PlayerCompanion::HasAimLOS( CBaseEntity *pAimTarget )
{
	trace_t tr;
	UTIL_TraceLine( Weapon_ShootPosition(), pAimTarget->WorldSpaceCenter(), MASK_SHOT, this, COLLISION_GROUP_NONE, &tr );

	if( tr.fraction < 0.5 || (tr.m_pEnt && (tr.m_pEnt->IsNPC()||tr.m_pEnt->IsPlayer())) )
	{
		return false;
	}

	return true;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_PlayerCompanion::AimGun()
{
	Vector vecAimDir;

	if( !GetEnemy() )
	{
		if( GetAimTarget() && FInViewCone(GetAimTarget()) )
		{
			float flDist; 
			Vector vecAimTargetLoc = GetAimTarget()->WorldSpaceCenter();

			flDist = (vecAimTargetLoc - GetAbsOrigin()).Length2DSqr();

			// Throw away a looktarget if it gets too close. We don't want guys turning around as
			// they walk through doorways which contain a looktarget.
			if( flDist < COMPANION_AIMTARGET_NEAREST_SQR )
			{
				StopAiming("Target too near");
				return;
			}

			// Aim at my target if it's in my cone
			vecAimDir = vecAimTargetLoc - Weapon_ShootPosition();;
			VectorNormalize( vecAimDir );
			SetAim( vecAimDir);

			if( !HasAimLOS(GetAimTarget()) )
			{
				// LOS is broken.
				if( !FindNewAimTarget() )
				{	
					// No alternative available right now. Stop aiming.
					StopAiming("No LOS");
				}
			}

			return;
		}
		else
		{
			if( GetAimTarget() )
			{
				// We're aiming at something, but we're about to stop because it's out of viewcone.
				// Try to find something else.
				if( FindNewAimTarget() )
				{
					// Found something else to aim at.
					return;
				}
				else
				{
					// ditch the aim target, it's gone out of view.
					StopAiming("Went out of view cone");
				}
			}

			if( GetReadinessLevel() == AIRL_AGITATED )
			{
				// Aim down! Agitated animations don't have non-aiming versions, so 
				// just point the weapon down.
				Vector vecSpot = EyePosition();
				Vector forward, up;
				GetVectors( &forward, NULL, &up );
				vecSpot += forward * 128 + up * -64;

				vecAimDir = vecSpot - Weapon_ShootPosition();
				VectorNormalize( vecAimDir );
				SetAim( vecAimDir);
				return;
			}
		}
	}

	BaseClass::AimGun();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
CBaseEntity *CNPC_PlayerCompanion::GetAlternateMoveShootTarget()
{
	if( GetAimTarget() && !GetAimTarget()->IsNPC() && GetReadinessLevel() != AIRL_RELAXED )
	{
		return GetAimTarget();
	}

	return BaseClass::GetAlternateMoveShootTarget();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CNPC_PlayerCompanion::IsValidEnemy( CBaseEntity *pEnemy )
{
	if ( GetFollowBehavior().GetFollowTarget() && GetFollowBehavior().GetFollowTarget()->IsPlayer() && IsSniper( pEnemy ) )
	{
		AI_EnemyInfo_t *pInfo = GetEnemies()->Find( pEnemy );
		if ( pInfo )
		{
			if ( gpGlobals->curtime - pInfo->timeLastSeen > 10 )
			{
				if ( !((CAI_BaseNPC*)pEnemy)->HasCondition( COND_IN_PVS ) )
					return false;
			}
		}
	}

	return BaseClass::IsValidEnemy( pEnemy );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CNPC_PlayerCompanion::IsSafeFromFloorTurret( const Vector &vecLocation, CBaseEntity *pTurret )
{
	float dist = ( vecLocation - pTurret->EyePosition() ).LengthSqr();

	if ( dist > Square( 4.0*12.0 ) )
	{
		if ( !pTurret->MyNPCPointer()->FInViewCone( vecLocation ) )
		{
#if 0 // Draws a green line to turrets I'm safe from
			NDebugOverlay::Line( vecLocation, pTurret->WorldSpaceCenter(), 0, 255, 0, false, 0.1 );
#endif 
			return true;
		}
	}

#if 0 // Draws a red lines to ones I'm not safe from.
	NDebugOverlay::Line( vecLocation, pTurret->WorldSpaceCenter(), 255, 0, 0, false, 0.1 );
#endif
	return false;
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
bool CNPC_PlayerCompanion::ShouldMoveAndShoot( void )
{
	return BaseClass::ShouldMoveAndShoot();
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
#define PC_LARGER_BURST_RANGE	(12.0f * 10.0f) // If an enemy is this close, player companions fire larger continuous bursts.
void CNPC_PlayerCompanion::OnUpdateShotRegulator()
{
	BaseClass::OnUpdateShotRegulator();

	if( GetEnemy() && HasCondition(COND_CAN_RANGE_ATTACK1) )
	{
		if( GetAbsOrigin().DistTo( GetEnemy()->GetAbsOrigin() ) <= PC_LARGER_BURST_RANGE )
		{
			if( hl2_episodic.GetBool() )
			{
				// Longer burst
				int longBurst = random->RandomInt( 10, 15 );
				GetShotRegulator()->SetBurstShotsRemaining( longBurst );
				GetShotRegulator()->SetRestInterval( 0.1, 0.2 );
			}
			else
			{
				// Longer burst
				GetShotRegulator()->SetBurstShotsRemaining( GetShotRegulator()->GetBurstShotsRemaining() * 2 );

				// Shorter Rest interval
				float flMinInterval, flMaxInterval;
				GetShotRegulator()->GetRestInterval( &flMinInterval, &flMaxInterval );
				GetShotRegulator()->SetRestInterval( flMinInterval * 0.6f, flMaxInterval * 0.6f );
			}
		}
	}
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
void CNPC_PlayerCompanion::DecalTrace( trace_t *pTrace, char const *decalName )
{
	// Do not decal a player companion's head or face, no matter what.
	if( pTrace->hitgroup == HITGROUP_HEAD )
		return;

	BaseClass::DecalTrace( pTrace, decalName );
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
bool CNPC_PlayerCompanion::FCanCheckAttacks()
{
	if( GetEnemy() && ( IsSniper(GetEnemy()) || IsMortar(GetEnemy()) || IsTurret(GetEnemy()) ) )
	{
		// Don't attack the sniper or the mortar.
		return false;
	}

	return BaseClass::FCanCheckAttacks();
}

//-----------------------------------------------------------------------------
// Purpose: Return the actual position the NPC wants to fire at when it's trying
//			to hit it's current enemy.
//-----------------------------------------------------------------------------
#define CITIZEN_HEADSHOT_FREQUENCY	3 // one in this many shots at a zombie will be aimed at the zombie's head
Vector CNPC_PlayerCompanion::GetActualShootPosition( const Vector &shootOrigin )
{
	if( GetEnemy() && GetEnemy()->Classify() == CLASS_ZOMBIE && random->RandomInt( 1, CITIZEN_HEADSHOT_FREQUENCY ) == 1 )
	{
		return GetEnemy()->HeadTarget( shootOrigin );
	}

	return BaseClass::GetActualShootPosition( shootOrigin );
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
WeaponProficiency_t CNPC_PlayerCompanion::CalcWeaponProficiency( CBaseCombatWeapon *pWeapon )
{
#ifdef MAPBASE
	if ( EntIsClass(pWeapon, gm_iszAR2Classname) )
#else
	if( FClassnameIs( pWeapon, "weapon_ar2" ) )
#endif
	{
		return WEAPON_PROFICIENCY_VERY_GOOD;
	}

	return WEAPON_PROFICIENCY_PERFECT;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CNPC_PlayerCompanion::Weapon_CanUse( CBaseCombatWeapon *pWeapon )
{
	if( BaseClass::Weapon_CanUse( pWeapon ) )
	{
		// If this weapon is a shotgun, take measures to control how many
		// are being used in this squad. Don't allow a companion to pick up
		// a shotgun if a squadmate already has one.
#ifdef MAPBASE
		if (EntIsClass(pWeapon, gm_iszShotgunClassname))
#else
		if( pWeapon->ClassMatches( gm_iszShotgunClassname ) )
#endif
		{
			return (NumWeaponsInSquad("weapon_shotgun") < 1 );
		}
		else
		{
			return true;
		}
	}

	return false;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CNPC_PlayerCompanion::ShouldLookForBetterWeapon()
{
	if ( m_bDontPickupWeapons )
		return false;

#ifdef MAPBASE
	// Now that citizens can holster weapons, they might look for a new one while unarmed.
	// Since that could already be worked around with OnHolster > DisableWeaponPickup, I decided to keep it that way in case it's desirable.

	// Don't look for a new weapon if we have secondary ammo for our current one.
	if (m_iNumGrenades > 0 && IsAltFireCapable() && GetActiveWeapon() && GetActiveWeapon()->UsesSecondaryAmmo())
		return false;
#endif

	return BaseClass::ShouldLookForBetterWeapon();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_PlayerCompanion::Weapon_Equip( CBaseCombatWeapon *pWeapon )
{
	BaseClass::Weapon_Equip( pWeapon );
	m_bReadinessCapable = IsReadinessCapable();
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
void CNPC_PlayerCompanion::PickupWeapon( CBaseCombatWeapon *pWeapon )
{
	BaseClass::PickupWeapon( pWeapon );
#ifdef MAPBASE
	SetPotentialSpeechTarget( pWeapon );
	SetSpeechTarget(pWeapon);
	SpeakIfAllowed( TLK_NEWWEAPON );
	m_OnWeaponPickup.FireOutput( pWeapon, this );
#else
	SpeakIfAllowed( TLK_NEWWEAPON );
	m_OnWeaponPickup.FireOutput( this, this );
#endif
}

#if COMPANION_MELEE_ATTACK
//-----------------------------------------------------------------------------
// Purpose: Cache user entity field values until spawn is called.
// Input  : szKeyName - Key to handle.
//			szValue - Value for key.
// Output : Returns true if the key was handled, false if not.
//-----------------------------------------------------------------------------
bool CNPC_PlayerCompanion::KeyValue( const char *szKeyName, const char *szValue )
{
	// MeleeAttack01 restoration, see CNPC_PlayerCompanion::MeleeAttack1Conditions
	if (FStrEq(szKeyName, "EnableMeleeAttack"))
	{
		if (!FStrEq(szValue, "0"))
			CapabilitiesAdd( bits_CAP_INNATE_MELEE_ATTACK1 );
		else
			CapabilitiesRemove( bits_CAP_INNATE_MELEE_ATTACK1 );

		return true;
	}

	return BaseClass::KeyValue( szKeyName, szValue );
}

//-----------------------------------------------------------------------------
// Purpose: For unused citizen melee attack (vorts might use this too)
// Input  :
// Output :
//-----------------------------------------------------------------------------
int CNPC_PlayerCompanion::MeleeAttack1Conditions ( float flDot, float flDist )
{
	if (!GetActiveWeapon())
		return COND_NONE;

	if (IsMoving())
	{
		// Is moving, cond_none
		return COND_NONE;
	}

	if (flDist > COMPANION_MELEE_DIST)
	{
		return COND_NONE; // COND_TOO_FAR_TO_ATTACK;
	}
	else if (flDot < 0.7)
	{
		return COND_NONE; // COND_NOT_FACING_ATTACK;
	}

	if (GetEnemy())
	{
		// Check Z
		if ( fabs(GetEnemy()->GetAbsOrigin().z - GetAbsOrigin().z) > 64 )
			return COND_NONE;

		if ( GetEnemy()->MyCombatCharacterPointer() && GetEnemy()->MyCombatCharacterPointer()->GetHullType() == HULL_TINY )
		{
			return COND_NONE;
		}
	}

	// Make sure not trying to kick through a window or something. 
	trace_t tr;
	Vector vecSrc, vecEnd;

	vecSrc = WorldSpaceCenter();
	vecEnd = GetEnemy()->WorldSpaceCenter();

	AI_TraceLine(vecSrc, vecEnd, MASK_SHOT, this, COLLISION_GROUP_NONE, &tr);
	if( tr.m_pEnt != GetEnemy() )
	{
		return COND_NONE;
	}

	return COND_CAN_MELEE_ATTACK1;
}
#endif

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

const int MAX_NON_SPECIAL_MULTICOVER = 2;

CUtlVector<AI_EnemyInfo_t *>	g_MultiCoverSearchEnemies;
CNPC_PlayerCompanion *			g_pMultiCoverSearcher;

//-------------------------------------

int __cdecl MultiCoverCompare( AI_EnemyInfo_t * const *ppLeft, AI_EnemyInfo_t * const *ppRight )
{
	const AI_EnemyInfo_t *pLeft = *ppLeft;
	const AI_EnemyInfo_t *pRight = *ppRight;

	if ( !pLeft->hEnemy && !pRight->hEnemy)
		return 0;

	if ( !pLeft->hEnemy )
		return 1;

	if ( !pRight->hEnemy )
		return -1;

	if ( pLeft->hEnemy == g_pMultiCoverSearcher->GetEnemy() )
		return -1;

	if ( pRight->hEnemy == g_pMultiCoverSearcher->GetEnemy() )
		return 1;

	bool bLeftIsSpecial = ( CNPC_PlayerCompanion::IsMortar( pLeft->hEnemy ) || CNPC_PlayerCompanion::IsSniper( pLeft->hEnemy ) );
	bool bRightIsSpecial = ( CNPC_PlayerCompanion::IsMortar( pLeft->hEnemy ) || CNPC_PlayerCompanion::IsSniper( pLeft->hEnemy ) );

	if ( !bLeftIsSpecial && bRightIsSpecial )
		return 1;

	if ( bLeftIsSpecial && !bRightIsSpecial )
		return -1;

	float leftRelevantTime = ( pLeft->timeLastSeen == AI_INVALID_TIME || pLeft->timeLastSeen == 0 ) ? -99999 : pLeft->timeLastSeen;
	if ( pLeft->timeLastReceivedDamageFrom != AI_INVALID_TIME && pLeft->timeLastReceivedDamageFrom > leftRelevantTime )
		leftRelevantTime = pLeft->timeLastReceivedDamageFrom;

	float rightRelevantTime = ( pRight->timeLastSeen == AI_INVALID_TIME || pRight->timeLastSeen == 0 ) ? -99999 : pRight->timeLastSeen;
	if ( pRight->timeLastReceivedDamageFrom != AI_INVALID_TIME && pRight->timeLastReceivedDamageFrom > rightRelevantTime )
		rightRelevantTime = pRight->timeLastReceivedDamageFrom;

	if ( leftRelevantTime < rightRelevantTime )
		return -1;

	if ( leftRelevantTime > rightRelevantTime )
		return 1;

	float leftDistSq = g_pMultiCoverSearcher->GetAbsOrigin().DistToSqr( pLeft->hEnemy->GetAbsOrigin() );
	float rightDistSq = g_pMultiCoverSearcher->GetAbsOrigin().DistToSqr( pRight->hEnemy->GetAbsOrigin() );

	if ( leftDistSq < rightDistSq )
		return -1;

	if ( leftDistSq > rightDistSq )
		return 1;

	return 0;
}

//-------------------------------------

void CNPC_PlayerCompanion::SetupCoverSearch( CBaseEntity *pEntity )
{
	if ( IsTurret( pEntity ) )
		gm_fCoverSearchType = CT_TURRET;
	
	gm_bFindingCoverFromAllEnemies = false;
	g_pMultiCoverSearcher = this;

	if ( Classify() == CLASS_PLAYER_ALLY_VITAL || IsInPlayerSquad() )
	{
		if ( GetEnemy() )
		{
			if ( !pEntity || GetEnemies()->NumEnemies() > 1 )
			{
				if ( !pEntity ) // if pEntity is NULL, test is against a point in space, so always to search against current enemy too
					gm_bFindingCoverFromAllEnemies = true;

				AIEnemiesIter_t iter;
				for ( AI_EnemyInfo_t *pEnemyInfo = GetEnemies()->GetFirst(&iter); pEnemyInfo != NULL; pEnemyInfo = GetEnemies()->GetNext(&iter) )
				{
					CBaseEntity *pEnemy = pEnemyInfo->hEnemy;
					if ( pEnemy )
					{
						if ( pEnemy != GetEnemy() )
						{
							if ( pEnemyInfo->timeAtFirstHand == AI_INVALID_TIME || gpGlobals->curtime - pEnemyInfo->timeLastSeen > 10.0 )
								continue;
							gm_bFindingCoverFromAllEnemies = true;
						}
						g_MultiCoverSearchEnemies.AddToTail( pEnemyInfo );
					}
				}

				if ( g_MultiCoverSearchEnemies.Count() == 0 )
				{
					gm_bFindingCoverFromAllEnemies = false;
				}
				else if ( gm_bFindingCoverFromAllEnemies )
				{
					g_MultiCoverSearchEnemies.Sort( MultiCoverCompare );
					Assert( g_MultiCoverSearchEnemies[0]->hEnemy == GetEnemy() );
				}
			}
		}
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_PlayerCompanion::CleanupCoverSearch()
{
	gm_fCoverSearchType = CT_NORMAL;
	g_MultiCoverSearchEnemies.RemoveAll();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CNPC_PlayerCompanion::FindCoverPos( CBaseEntity *pEntity, Vector *pResult)
{
	AI_PROFILE_SCOPE(CNPC_PlayerCompanion_FindCoverPos);

	ASSERT_NO_REENTRY();

	bool result = false;

	SetupCoverSearch( pEntity );
	
	if ( gm_bFindingCoverFromAllEnemies )
	{
		result = BaseClass::FindCoverPos( pEntity, pResult );
		gm_bFindingCoverFromAllEnemies = false;
	}
	
	if ( !result )
		result = BaseClass::FindCoverPos( pEntity, pResult );
	
	CleanupCoverSearch();

	return result;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

bool CNPC_PlayerCompanion::FindCoverPosInRadius( CBaseEntity *pEntity, const Vector &goalPos, float coverRadius, Vector *pResult )
{
	AI_PROFILE_SCOPE(CNPC_PlayerCompanion_FindCoverPosInRadius);

	ASSERT_NO_REENTRY();

	bool result = false;

	SetupCoverSearch( pEntity );

	if ( gm_bFindingCoverFromAllEnemies )
	{
		result = BaseClass::FindCoverPosInRadius( pEntity, goalPos, coverRadius, pResult );
		gm_bFindingCoverFromAllEnemies = false;
	}

	if ( !result )
	{
		result = BaseClass::FindCoverPosInRadius( pEntity, goalPos, coverRadius, pResult );
	}
	
	CleanupCoverSearch();

	return result;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

bool CNPC_PlayerCompanion::FindCoverPos( CSound *pSound, Vector *pResult )
{
	AI_PROFILE_SCOPE(CNPC_PlayerCompanion_FindCoverPos);

	bool result = false;
	bool bIsMortar = ( pSound->SoundContext() == SOUND_CONTEXT_MORTAR );

	SetupCoverSearch( NULL );

	if ( gm_bFindingCoverFromAllEnemies )
	{
		result = ( bIsMortar ) ? FindMortarCoverPos( pSound, pResult ) : 
								 BaseClass::FindCoverPos( pSound, pResult );
		gm_bFindingCoverFromAllEnemies = false;
	}

	if ( !result )
	{
		result = ( bIsMortar ) ? FindMortarCoverPos( pSound, pResult ) : 
								 BaseClass::FindCoverPos( pSound, pResult );
	}

	CleanupCoverSearch();

	return result;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

bool CNPC_PlayerCompanion::FindMortarCoverPos( CSound *pSound, Vector *pResult )
{
	bool result = false;

	Assert( pSound->SoundContext() == SOUND_CONTEXT_MORTAR );
	gm_fCoverSearchType = CT_MORTAR;
	result = GetTacticalServices()->FindLateralCover( pSound->GetSoundOrigin(), 0, pResult );
	if ( !result )
	{
		result = GetTacticalServices()->FindCoverPos( pSound->GetSoundOrigin(), 
													  pSound->GetSoundOrigin(), 
													  0, 
													  CoverRadius(), 
													  pResult );
	}
	gm_fCoverSearchType = CT_NORMAL;
	
	return result;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CNPC_PlayerCompanion::IsCoverPosition( const Vector &vecThreat, const Vector &vecPosition )
{
	if ( gm_bFindingCoverFromAllEnemies )
	{
		for ( int i = 0; i < g_MultiCoverSearchEnemies.Count(); i++ )
		{
			// @TODO (toml 07-27-04): Should skip checking points near already checked points
			AI_EnemyInfo_t *pEnemyInfo = g_MultiCoverSearchEnemies[i];
			Vector testPos;
			CBaseEntity *pEnemy = pEnemyInfo->hEnemy;
			if ( !pEnemy )
				continue;

			if ( pEnemy == GetEnemy() || IsMortar( pEnemy ) || IsSniper( pEnemy ) || i < MAX_NON_SPECIAL_MULTICOVER )
			{
				testPos = pEnemyInfo->vLastKnownLocation + pEnemy->GetViewOffset();
			}
			else
				break;

			gm_bFindingCoverFromAllEnemies = false;
			bool result = IsCoverPosition( testPos, vecPosition );
			gm_bFindingCoverFromAllEnemies = true;
			
			if ( !result )
				return false;
		}

		if ( gm_fCoverSearchType != CT_MORTAR &&  GetEnemy() && vecThreat.DistToSqr( GetEnemy()->EyePosition() ) < 1 )
			return true;

		// else fall through
	}

	if ( gm_fCoverSearchType == CT_TURRET && GetEnemy() && IsSafeFromFloorTurret( vecPosition, GetEnemy() ) )
	{
		return true;
	}

	if ( gm_fCoverSearchType == CT_MORTAR )
	{
		CSound *pSound = GetBestSound( SOUND_DANGER );
		Assert ( pSound && pSound->SoundContext() == SOUND_CONTEXT_MORTAR );
		if( pSound  )
		{
			// Don't get closer to the shell
			Vector vecToSound = vecThreat - GetAbsOrigin();
			Vector vecToPosition = vecPosition - GetAbsOrigin();
			VectorNormalize( vecToPosition );
			VectorNormalize( vecToSound );

			if ( vecToPosition.AsVector2D().Dot( vecToSound.AsVector2D() ) > 0 )
				return false;

			// Anything outside the radius is okay
			float flDistSqr = (vecPosition - vecThreat).Length2DSqr();
			float radiusSq = Square( pSound->Volume() );
			if( flDistSqr > radiusSq )
			{
				return true;
			}
		}
	}

	return BaseClass::IsCoverPosition( vecThreat, vecPosition );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CNPC_PlayerCompanion::IsMortar( CBaseEntity *pEntity )
{
	if ( !pEntity )
		return false;
	CBaseEntity *pEntityParent = pEntity->GetParent();
	return ( pEntityParent && pEntityParent->GetClassname() == STRING(gm_iszMortarClassname) );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CNPC_PlayerCompanion::IsSniper( CBaseEntity *pEntity )
{
	if ( !pEntity )
		return false;
	return ( pEntity->Classify() == CLASS_PROTOSNIPER );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CNPC_PlayerCompanion::IsTurret( CBaseEntity *pEntity )
{
	if ( !pEntity )
		return false;
	const char *pszClassname = pEntity->GetClassname();
	return ( pszClassname == STRING(gm_iszFloorTurretClassname) || pszClassname == STRING(gm_iszGroundTurretClassname) );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CNPC_PlayerCompanion::IsGunship( CBaseEntity *pEntity )
{
	if( !pEntity )
		return false;
	return (pEntity->Classify() == CLASS_COMBINE_GUNSHIP );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
int CNPC_PlayerCompanion::OnTakeDamage_Alive( const CTakeDamageInfo &info )
{
	if( info.GetAttacker() )
	{
		bool bIsEnvFire;
		if( ( bIsEnvFire = FClassnameIs( info.GetAttacker(), "env_fire" ) ) != false || FClassnameIs( info.GetAttacker(), "entityflame" ) || FClassnameIs( info.GetAttacker(), "env_entity_igniter" ) )
		{
			GetMotor()->SetIdealYawToTarget( info.GetAttacker()->GetAbsOrigin() );
			SetCondition( COND_PC_HURTBYFIRE );
		}

		// @Note (toml 07-25-04): there isn't a good solution to player companions getting injured by
		//						  fires that have huge damage radii that extend outside the rendered
		//						  fire. Recovery from being injured by fire will also not be done
		//						  before we ship/ Here we trade one bug (guys standing around dying
		//						  from flames they appear to not be near), for a lesser one
		//						  this guy was standing in a fire and didn't react. Since
		//						  the levels are supposed to have the centers of all the fires
		//						  npc clipped, this latter case should be rare.
		if ( bIsEnvFire )
		{
			if ( ( GetAbsOrigin() - info.GetAttacker()->GetAbsOrigin() ).Length2DSqr() > Square(12 + GetHullWidth() * .5 ) )
			{
				return 0;
			}
		}
	}

	return BaseClass::OnTakeDamage_Alive( info );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_PlayerCompanion::OnFriendDamaged( CBaseCombatCharacter *pSquadmate, CBaseEntity *pAttackerEnt )
{
	AI_PROFILE_SCOPE( CNPC_PlayerCompanion_OnFriendDamaged );
	BaseClass::OnFriendDamaged( pSquadmate, pAttackerEnt );

	CAI_BaseNPC *pAttacker = pAttackerEnt->MyNPCPointer();
	if ( pAttacker )
	{
		bool bDirect = ( pSquadmate->FInViewCone(pAttacker) &&
						 ( ( pSquadmate->IsPlayer() && HasCondition(COND_SEE_PLAYER) ) || 
						 ( pSquadmate->MyNPCPointer() && pSquadmate->MyNPCPointer()->IsPlayerAlly() && 
						   GetSenses()->DidSeeEntity( pSquadmate ) ) ) );
		if ( bDirect )
		{
			UpdateEnemyMemory( pAttacker, pAttacker->GetAbsOrigin(), pSquadmate );
		}
		else
		{
			if ( FVisible( pSquadmate ) )
			{
				AI_EnemyInfo_t *pInfo = GetEnemies()->Find( pAttacker );
				if ( !pInfo || ( gpGlobals->curtime - pInfo->timeLastSeen ) > 15.0 )
					UpdateEnemyMemory( pAttacker, pSquadmate->GetAbsOrigin(), pSquadmate );
			}
		}

		CBasePlayer *pPlayer = AI_GetSinglePlayer();
		if ( pPlayer && IsInPlayerSquad() && ( pPlayer->GetAbsOrigin().AsVector2D() - GetAbsOrigin().AsVector2D() ).LengthSqr() < Square( 25*12 ) && IsAllowedToSpeak( TLK_WATCHOUT ) )
		{
			if ( !pPlayer->FInViewCone( pAttacker ) )
			{
				Vector2D vPlayerDir = pPlayer->EyeDirection2D().AsVector2D();
				Vector2D vEnemyDir = pAttacker->EyePosition().AsVector2D() - pPlayer->EyePosition().AsVector2D();
				vEnemyDir.NormalizeInPlace();
				float dot = vPlayerDir.Dot( vEnemyDir );
				if ( dot < 0 )
					Speak( TLK_WATCHOUT, "dangerloc:behind" );
				else if ( ( pPlayer->GetAbsOrigin().AsVector2D() - pAttacker->GetAbsOrigin().AsVector2D() ).LengthSqr() > Square( 40*12 ) )
					Speak( TLK_WATCHOUT, "dangerloc:far" );
			}
			else if ( pAttacker->GetAbsOrigin().z - pPlayer->GetAbsOrigin().z > 128 )
			{
				Speak( TLK_WATCHOUT, "dangerloc:above" );
			}
			else if ( pAttacker->GetHullType() <= HULL_TINY && ( pPlayer->GetAbsOrigin().AsVector2D() - pAttacker->GetAbsOrigin().AsVector2D() ).LengthSqr() > Square( 100*12 ) )
			{
				Speak( TLK_WATCHOUT, "dangerloc:far" );
			}
		}
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CNPC_PlayerCompanion::IsValidMoveAwayDest( const Vector &vecDest )
{
	// Don't care what the destination is unless I have an enemy and 
	// that enemy is a sniper (for now).
	if( !GetEnemy() )
	{
		return true;
	}

	if( GetEnemy()->Classify() != CLASS_PROTOSNIPER )
	{
		return true;
	}

	if( IsCoverPosition( GetEnemy()->EyePosition(), vecDest + GetViewOffset() ) )
	{
		return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CNPC_PlayerCompanion::FValidateHintType( CAI_Hint *pHint )
{
	switch( pHint->HintType() )
	{
	case HINT_PLAYER_SQUAD_TRANSITON_POINT:
	case HINT_WORLD_VISUALLY_INTERESTING_DONT_AIM:
	case HINT_PLAYER_ALLY_MOVE_AWAY_DEST:
	case HINT_PLAYER_ALLY_FEAR_DEST:
		return true;
		break;

	default:
		break;
	}

	return BaseClass::FValidateHintType( pHint );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CNPC_PlayerCompanion::ValidateNavGoal()
{
	bool result;
	if ( GetNavigator()->GetGoalType() == GOALTYPE_COVER )
	{
		if ( IsEnemyTurret() )
			gm_fCoverSearchType = CT_TURRET;
	}
	result = BaseClass::ValidateNavGoal();
	gm_fCoverSearchType = CT_NORMAL;
	return result;
}

const float AVOID_TEST_DIST = 18.0f*12.0f;

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
#define COMPANION_EPISODIC_AVOID_ENTITY_FLAME_RADIUS	18.0f
bool CNPC_PlayerCompanion::OverrideMove( float flInterval )
{
	bool overrode = BaseClass::OverrideMove( flInterval );

	if ( !overrode && GetNavigator()->GetGoalType() != GOALTYPE_NONE )
	{
#ifdef MAPBASE
		#define iszEnvFire gm_isz_class_EnvFire
#else
		string_t iszEnvFire = AllocPooledString( "env_fire" );
#endif
		string_t iszBounceBomb = AllocPooledString( "combine_mine" );

#ifdef HL2_EPISODIC			
#ifdef MAPBASE
		#define iszNPCTurretFloor gm_isz_class_FloorTurret
#else
		string_t iszNPCTurretFloor = AllocPooledString( "npc_turret_floor" );
#endif
		string_t iszEntityFlame = AllocPooledString( "entityflame" );
#endif // HL2_EPISODIC

		if ( IsCurSchedule( SCHED_TAKE_COVER_FROM_BEST_SOUND ) )
		{
			CSound *pSound = GetBestSound( SOUND_DANGER );
			if( pSound && pSound->SoundContext() == SOUND_CONTEXT_MORTAR )
			{
				// Try not to get any closer to the center
				GetLocalNavigator()->AddObstacle( pSound->GetSoundOrigin(), (pSound->GetSoundOrigin() - GetAbsOrigin()).Length2D() * 0.5, AIMST_AVOID_DANGER );
			}
		}

		CBaseEntity *pEntity = NULL;
		trace_t tr;
		
		// For each possible entity, compare our known interesting classnames to its classname, via ID
		while( ( pEntity = OverrideMoveCache_FindTargetsInRadius( pEntity, GetAbsOrigin(), AVOID_TEST_DIST ) ) != NULL )
		{
			// Handle each type
			if ( pEntity->m_iClassname == iszEnvFire )
			{
				Vector vMins, vMaxs;
				if ( FireSystem_GetFireDamageDimensions( pEntity, &vMins, &vMaxs ) )
				{
					UTIL_TraceLine( WorldSpaceCenter(), pEntity->WorldSpaceCenter(), MASK_FIRE_SOLID, pEntity, COLLISION_GROUP_NONE, &tr );
					if (tr.fraction == 1.0 && !tr.startsolid)
					{
						GetLocalNavigator()->AddObstacle( pEntity->GetAbsOrigin(), ( ( vMaxs.x - vMins.x ) * 1.414 * 0.5 ) + 6.0, AIMST_AVOID_DANGER );
					}
				}
			}
#ifdef HL2_EPISODIC			
			else if ( pEntity->m_iClassname == iszNPCTurretFloor )
			{
				UTIL_TraceLine( WorldSpaceCenter(), pEntity->WorldSpaceCenter(), MASK_BLOCKLOS, pEntity, COLLISION_GROUP_NONE, &tr );
				if (tr.fraction == 1.0 && !tr.startsolid)
				{
					float radius = 1.4 * pEntity->CollisionProp()->BoundingRadius2D(); 
					GetLocalNavigator()->AddObstacle( pEntity->WorldSpaceCenter(), radius, AIMST_AVOID_OBJECT );
				}
			}
			else if( pEntity->m_iClassname == iszEntityFlame && pEntity->GetParent() && !pEntity->GetParent()->IsNPC() )
			{
				float flDist = pEntity->WorldSpaceCenter().DistTo( WorldSpaceCenter() );

				if( flDist > COMPANION_EPISODIC_AVOID_ENTITY_FLAME_RADIUS )
				{
					// If I'm not in the flame, prevent me from getting close to it.
					// If I AM in the flame, avoid placing an obstacle until the flame frightens me away from itself.
					UTIL_TraceLine( WorldSpaceCenter(), pEntity->WorldSpaceCenter(), MASK_BLOCKLOS, pEntity, COLLISION_GROUP_NONE, &tr );
					if (tr.fraction == 1.0 && !tr.startsolid)
					{
						GetLocalNavigator()->AddObstacle( pEntity->WorldSpaceCenter(), COMPANION_EPISODIC_AVOID_ENTITY_FLAME_RADIUS, AIMST_AVOID_OBJECT );
					}
				}
			}
#endif // HL2_EPISODIC
			else if ( pEntity->m_iClassname == iszBounceBomb )
			{
				CBounceBomb *pBomb = static_cast<CBounceBomb *>(pEntity);
				if ( pBomb && pBomb->ShouldBeAvoidedByCompanions() )
				{
					UTIL_TraceLine( WorldSpaceCenter(), pEntity->WorldSpaceCenter(), MASK_BLOCKLOS, pEntity, COLLISION_GROUP_NONE, &tr );
					if (tr.fraction == 1.0 && !tr.startsolid)
					{
						GetLocalNavigator()->AddObstacle( pEntity->GetAbsOrigin(), BOUNCEBOMB_DETONATE_RADIUS * .8, AIMST_AVOID_DANGER );
					}
				}
			}
		}
	}

	return overrode;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CNPC_PlayerCompanion::MovementCost( int moveType, const Vector &vecStart, const Vector &vecEnd, float *pCost )
{
	bool bResult = BaseClass::MovementCost( moveType, vecStart, vecEnd, pCost );
	if ( moveType == bits_CAP_MOVE_GROUND )
	{
		if ( IsCurSchedule( SCHED_TAKE_COVER_FROM_BEST_SOUND ) )
		{
			CSound *pSound = GetBestSound( SOUND_DANGER );
			if( pSound && (pSound->SoundContext() & (SOUND_CONTEXT_MORTAR|SOUND_CONTEXT_FROM_SNIPER)) )
			{
				Vector vecToSound = pSound->GetSoundReactOrigin() - GetAbsOrigin();
				Vector vecToPosition = vecEnd - GetAbsOrigin();
				VectorNormalize( vecToPosition );
				VectorNormalize( vecToSound );

				if ( vecToPosition.AsVector2D().Dot( vecToSound.AsVector2D() ) > 0 )
				{
					*pCost *= 1.5;
					bResult = true;
				}
			}
		}

		if ( m_bWeightPathsInCover && GetEnemy() )
		{
			if ( BaseClass::IsCoverPosition( GetEnemy()->EyePosition(), vecEnd ) )
			{
				*pCost *= 0.1;
				bResult = true;
			}
		}
	}
	return bResult;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
float CNPC_PlayerCompanion::GetIdealSpeed() const
{
	float baseSpeed = BaseClass::GetIdealSpeed();

	if ( baseSpeed < m_flBoostSpeed )
		return m_flBoostSpeed;

	return baseSpeed;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
float CNPC_PlayerCompanion::GetIdealAccel() const
{
	float multiplier = 1.0;
	if ( AI_IsSinglePlayer() )
	{
		if ( m_bMovingAwayFromPlayer && (UTIL_PlayerByIndex(1)->GetAbsOrigin() - GetAbsOrigin()).Length2DSqr() < Square(3.0*12.0) )
			multiplier = 2.0;
	}
	return BaseClass::GetIdealAccel() * multiplier;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CNPC_PlayerCompanion::OnObstructionPreSteer( AILocalMoveGoal_t *pMoveGoal, float distClear, AIMoveResult_t *pResult )
{
	if ( pMoveGoal->directTrace.flTotalDist - pMoveGoal->directTrace.flDistObstructed < GetHullWidth() * 1.5 )
	{
		CAI_BaseNPC *pBlocker = pMoveGoal->directTrace.pObstruction->MyNPCPointer();
		if ( pBlocker && pBlocker->IsPlayerAlly() && !pBlocker->IsMoving() && !pBlocker->IsInAScript() &&
			 ( IsCurSchedule( SCHED_NEW_WEAPON ) || 
			   IsCurSchedule( SCHED_GET_HEALTHKIT ) || 
			   pBlocker->IsCurSchedule( SCHED_FAIL ) || 
			   ( IsInPlayerSquad() && !pBlocker->IsInPlayerSquad() ) ||
			   Classify() == CLASS_PLAYER_ALLY_VITAL ||
			   IsInAScript() ) )

		{
			if ( pBlocker->ConditionInterruptsCurSchedule( COND_GIVE_WAY ) || 
				 pBlocker->ConditionInterruptsCurSchedule( COND_PLAYER_PUSHING ) )
			{
				// HACKHACK
				pBlocker->GetMotor()->SetIdealYawToTarget( WorldSpaceCenter() );
				pBlocker->SetSchedule( SCHED_MOVE_AWAY );
			}

		}
	}

	if ( pMoveGoal->directTrace.pObstruction )
	{
	}

	return BaseClass::OnObstructionPreSteer( pMoveGoal, distClear, pResult );
}

//-----------------------------------------------------------------------------
// Purpose: Whether or not we should always transition with the player
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CNPC_PlayerCompanion::ShouldAlwaysTransition( void )
{
	// No matter what, come through
	if ( m_bAlwaysTransition )
		return true;

	// Squadmates always come with
	if ( IsInPlayerSquad() )
		return true;

	// If we're following the player, then come along
	if ( GetFollowBehavior().GetFollowTarget() && GetFollowBehavior().GetFollowTarget()->IsPlayer() )
		return true;

	return false;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_PlayerCompanion::InputOutsideTransition( inputdata_t &inputdata )
{
	if ( !AI_IsSinglePlayer() )
		return;

	// Must want to do this
	if ( ShouldAlwaysTransition() == false )
		return;

	// If we're in a vehicle, that vehicle will transition with us still inside (which is preferable)
	if ( IsInAVehicle() )
		return;

	CBaseEntity *pPlayer = UTIL_GetLocalPlayer();
	const Vector &playerPos = pPlayer->GetAbsOrigin();

	// Mark us as already having succeeded if we're vital or always meant to come with the player
	bool bAlwaysTransition = ( ( Classify() == CLASS_PLAYER_ALLY_VITAL ) || m_bAlwaysTransition );
	bool bPathToPlayer = bAlwaysTransition;

	if ( bAlwaysTransition == false )
	{
		AI_Waypoint_t *pPathToPlayer = GetPathfinder()->BuildRoute( GetAbsOrigin(), playerPos, pPlayer, 0 );

		if ( pPathToPlayer )
		{
			bPathToPlayer = true;
			CAI_Path tempPath;
			tempPath.SetWaypoints( pPathToPlayer ); // path object will delete waypoints
			GetPathfinder()->UnlockRouteNodes( pPathToPlayer );
		}
	}


#ifdef USE_PATHING_LENGTH_REQUIREMENT_FOR_TELEPORT
	float pathLength = tempPath.GetPathDistanceToGoal( GetAbsOrigin() );

	if ( pathLength > 150 * 12 )
		return;
#endif

	bool bMadeIt = false;
	Vector teleportLocation;

	CAI_Hint *pHint = CAI_HintManager::FindHint( this, HINT_PLAYER_SQUAD_TRANSITON_POINT, bits_HINT_NODE_NEAREST, PLAYERCOMPANION_TRANSITION_SEARCH_DISTANCE, &playerPos );
	while ( pHint )
	{
		pHint->Lock(this);
		pHint->Unlock(0.5); // prevent other squadmates and self from using during transition. 

		pHint->GetPosition( GetHullType(), &teleportLocation );
		if ( GetNavigator()->CanFitAtPosition( teleportLocation, MASK_NPCSOLID ) )
		{
			bMadeIt = true;
			if ( !bPathToPlayer && ( playerPos - GetAbsOrigin() ).LengthSqr() > Square(40*12) )
			{
				AI_Waypoint_t *pPathToTeleport = GetPathfinder()->BuildRoute( GetAbsOrigin(), teleportLocation, pPlayer, 0 );

				if ( !pPathToTeleport )
				{
					DevMsg( 2, "NPC \"%s\" failed to teleport to transition a point because there is no path\n", STRING(GetEntityName()) );
					bMadeIt = false;
				}
				else
				{
					CAI_Path tempPath;
					GetPathfinder()->UnlockRouteNodes( pPathToTeleport );
					tempPath.SetWaypoints( pPathToTeleport ); // path object will delete waypoints
				}
			}

			if ( bMadeIt )
			{
				DevMsg( 2, "NPC \"%s\" teleported to transition point %d\n", STRING(GetEntityName()), pHint->GetNodeId() );
				break;
			}
		}
		else
		{
			if ( g_debug_transitions.GetBool() )
			{
				NDebugOverlay::Box( teleportLocation, GetHullMins(), GetHullMaxs(), 255,0,0, 8, 999 );
			}
		}
		pHint = CAI_HintManager::FindHint( this, HINT_PLAYER_SQUAD_TRANSITON_POINT, bits_HINT_NODE_NEAREST, PLAYERCOMPANION_TRANSITION_SEARCH_DISTANCE, &playerPos );
	}
	if ( !bMadeIt )
	{
		// Force us if we didn't find a normal route
		if ( bAlwaysTransition )
		{
			bMadeIt = FindSpotForNPCInRadius( &teleportLocation, pPlayer->GetAbsOrigin(), this, 32.0*1.414, true );
			if ( !bMadeIt )
				bMadeIt = FindSpotForNPCInRadius( &teleportLocation, pPlayer->GetAbsOrigin(), this, 32.0*1.414, false );
		}
	}

	if ( bMadeIt )
	{
		Teleport( &teleportLocation, NULL, NULL );
	}
	else
	{
		DevMsg( 2, "NPC \"%s\" failed to find a suitable transition a point\n", STRING(GetEntityName()) );
	}

	BaseClass::InputOutsideTransition( inputdata );
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
void CNPC_PlayerCompanion::InputSetReadinessPanic( inputdata_t &inputdata )
{
	SetReadinessLevel( AIRL_PANIC, true, true );
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
void CNPC_PlayerCompanion::InputSetReadinessStealth( inputdata_t &inputdata )
{
	SetReadinessLevel( AIRL_STEALTH, true, true );
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
void CNPC_PlayerCompanion::InputSetReadinessLow( inputdata_t &inputdata )
{
	SetReadinessLevel( AIRL_RELAXED, true, true );
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
void CNPC_PlayerCompanion::InputSetReadinessMedium( inputdata_t &inputdata )
{
	SetReadinessLevel( AIRL_STIMULATED, true, true );
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
void CNPC_PlayerCompanion::InputSetReadinessHigh( inputdata_t &inputdata )
{
	SetReadinessLevel( AIRL_AGITATED, true, true );
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
void CNPC_PlayerCompanion::InputLockReadiness( inputdata_t &inputdata )
{
	float value = inputdata.value.Float();
	LockReadiness( value );
}

//-----------------------------------------------------------------------------
// Purpose: Locks the readiness state of the NCP
// Input  : time - if -1, the lock is effectively infinite
//-----------------------------------------------------------------------------
void CNPC_PlayerCompanion::LockReadiness( float duration )
{
	if ( duration == -1.0f )
	{
		m_flReadinessLockedUntil = FLT_MAX;
	}
	else
	{
		m_flReadinessLockedUntil = gpGlobals->curtime + duration;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Unlocks the readiness state
//-----------------------------------------------------------------------------
void CNPC_PlayerCompanion::UnlockReadiness( void )
{
	// Set to the past
	m_flReadinessLockedUntil = gpGlobals->curtime - 0.1f;
}

//------------------------------------------------------------------------------
#ifdef HL2_EPISODIC

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CNPC_PlayerCompanion::ShouldDeferToPassengerBehavior( void )
{
	if ( m_PassengerBehavior.CanSelectSchedule() )
		return true;

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Determines if this player companion is capable of entering a vehicle
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CNPC_PlayerCompanion::CanEnterVehicle( void )
{
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CNPC_PlayerCompanion::CanExitVehicle( void )
{
	// See if we can exit our vehicle
	CPropJeepEpisodic *pVehicle = dynamic_cast<CPropJeepEpisodic *>(m_PassengerBehavior.GetTargetVehicle());
	if ( pVehicle != NULL && pVehicle->NPC_CanExitVehicle( this, true ) == false )
		return false;

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *lpszVehicleName - 
//-----------------------------------------------------------------------------
void CNPC_PlayerCompanion::EnterVehicle( CBaseEntity *pEntityVehicle, bool bImmediately )
{
	// Must be allowed to do this
	if ( CanEnterVehicle() == false )
		return;

	// Find the target vehicle
	CPropJeepEpisodic *pVehicle = dynamic_cast<CPropJeepEpisodic *>(pEntityVehicle);

	// Get in the car if it's valid
	if ( pVehicle != NULL && pVehicle->NPC_CanEnterVehicle( this, true ) )
	{
		// Set her into a "passenger" behavior
		m_PassengerBehavior.Enable( pVehicle, bImmediately );
		m_PassengerBehavior.EnterVehicle();

		// Only do this if we're outside the vehicle
		if ( m_PassengerBehavior.GetPassengerState() == PASSENGER_STATE_OUTSIDE )
		{
			SetCondition( COND_PC_BECOMING_PASSENGER );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Get into the requested vehicle
// Input  : &inputdata - contains the entity name of the vehicle to enter
//-----------------------------------------------------------------------------
void CNPC_PlayerCompanion::InputEnterVehicle( inputdata_t &inputdata )
{
	CBaseEntity *pEntity = FindNamedEntity( inputdata.value.String() );
	EnterVehicle( pEntity, false );
}

//-----------------------------------------------------------------------------
// Purpose: Get into the requested vehicle immediately (no animation, pop)
// Input  : &inputdata - contains the entity name of the vehicle to enter
//-----------------------------------------------------------------------------
void CNPC_PlayerCompanion::InputEnterVehicleImmediately( inputdata_t &inputdata )
{
	CBaseEntity *pEntity = FindNamedEntity( inputdata.value.String() );
	EnterVehicle( pEntity, true );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_PlayerCompanion::InputExitVehicle( inputdata_t &inputdata )
{
	// See if we're allowed to exit the vehicle
	if ( CanExitVehicle() == false )
		return;

	m_PassengerBehavior.ExitVehicle();
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &inputdata - 
//-----------------------------------------------------------------------------
void CNPC_PlayerCompanion::InputCancelEnterVehicle( inputdata_t &inputdata )
{
	m_PassengerBehavior.CancelEnterVehicle();
}

//-----------------------------------------------------------------------------
// Purpose: Forces the NPC out of the vehicle they're riding in
// Input  : bImmediate - If we need to exit immediately, teleport to any exit location
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CNPC_PlayerCompanion::ExitVehicle( void )
{
	// For now just get out
	m_PassengerBehavior.ExitVehicle();
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CNPC_PlayerCompanion::IsInAVehicle( void ) const
{
	// Must be active and getting in/out of vehicle
	if ( m_PassengerBehavior.IsEnabled() && m_PassengerBehavior.GetPassengerState() != PASSENGER_STATE_OUTSIDE )
		return true;

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : IServerVehicle - 
//-----------------------------------------------------------------------------
IServerVehicle *CNPC_PlayerCompanion::GetVehicle( void )
{
	if ( IsInAVehicle() )
	{
		CPropVehicleDriveable *pDriveableVehicle = m_PassengerBehavior.GetTargetVehicle();
		if ( pDriveableVehicle != NULL )
			return pDriveableVehicle->GetServerVehicle();
	}

	return NULL;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : CBaseEntity
//-----------------------------------------------------------------------------
CBaseEntity *CNPC_PlayerCompanion::GetVehicleEntity( void )
{
	if ( IsInAVehicle() )
	{
		CPropVehicleDriveable *pDriveableVehicle = m_PassengerBehavior.GetTargetVehicle();
			return pDriveableVehicle;
	}

	return NULL;
}

//-----------------------------------------------------------------------------
// Purpose: Override our efficiency so that we don't jitter when we're in the middle
//			of our enter/exit animations.
// Input  : bInPVS - Whether we're in the PVS or not
//-----------------------------------------------------------------------------
void CNPC_PlayerCompanion::UpdateEfficiency( bool bInPVS )
{ 
	// If we're transitioning and in the PVS, we override our efficiency
	if ( IsInAVehicle() && bInPVS )
	{
		PassengerState_e nState = m_PassengerBehavior.GetPassengerState();
		if ( nState == PASSENGER_STATE_ENTERING || nState == PASSENGER_STATE_EXITING )
		{
			SetEfficiency( AIE_NORMAL );
			return;
		}
	}

	// Do the default behavior
	BaseClass::UpdateEfficiency( bInPVS );
}

//-----------------------------------------------------------------------------
// Purpose: Whether or not we can dynamically interact with another NPC
//-----------------------------------------------------------------------------
bool CNPC_PlayerCompanion::CanRunAScriptedNPCInteraction( bool bForced /*= false*/ )
{
	// TODO: Allow this but only for interactions who stem from being in a vehicle?
	if ( IsInAVehicle() )
		return false;

	return BaseClass::CanRunAScriptedNPCInteraction( bForced );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CNPC_PlayerCompanion::IsAllowedToDodge( void )
{
	// TODO: Allow this but only for interactions who stem from being in a vehicle?
	if ( IsInAVehicle() )
		return false;

	return BaseClass::IsAllowedToDodge();
}

#endif	//HL2_EPISODIC
//------------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose: Always transition along with the player
//-----------------------------------------------------------------------------
void CNPC_PlayerCompanion::InputEnableAlwaysTransition( inputdata_t &inputdata )
{
	m_bAlwaysTransition = true;
}

//-----------------------------------------------------------------------------
// Purpose: Stop always transitioning along with the player
//-----------------------------------------------------------------------------
void CNPC_PlayerCompanion::InputDisableAlwaysTransition( inputdata_t &inputdata )
{
	m_bAlwaysTransition = false;
}

//-----------------------------------------------------------------------------
// Purpose: Stop picking up weapons from the ground
//-----------------------------------------------------------------------------
void CNPC_PlayerCompanion::InputEnableWeaponPickup( inputdata_t &inputdata )
{
	m_bDontPickupWeapons = false;
}

//-----------------------------------------------------------------------------
// Purpose: Return to default behavior of picking up better weapons on the ground
//-----------------------------------------------------------------------------
void CNPC_PlayerCompanion::InputDisableWeaponPickup( inputdata_t &inputdata )
{
	m_bDontPickupWeapons = true;
}

#ifndef MAPBASE // See CAI_BaseNPC::InputGiveWeapon()
//------------------------------------------------------------------------------
// Purpose: Give the NPC in question the weapon specified
//------------------------------------------------------------------------------
void CNPC_PlayerCompanion::InputGiveWeapon( inputdata_t &inputdata )
{
	// Give the NPC the specified weapon
	string_t iszWeaponName = inputdata.value.StringID();
	if ( iszWeaponName != NULL_STRING )
	{
		if( Classify() == CLASS_PLAYER_ALLY_VITAL )
		{
			m_iszPendingWeapon = iszWeaponName;
		}
		else
		{
			GiveWeapon( iszWeaponName );
		}
	}
}
#endif

#if HL2_EPISODIC
//------------------------------------------------------------------------------
// Purpose: Delete all outputs from this NPC.
//------------------------------------------------------------------------------
void CNPC_PlayerCompanion::InputClearAllOuputs( inputdata_t &inputdata )
{
	datamap_t *dmap = GetDataDescMap();
	while ( dmap )
	{
		int fields = dmap->dataNumFields;
		for ( int i = 0; i < fields; i++ )
		{
			typedescription_t *dataDesc = &dmap->dataDesc[i];
			if ( ( dataDesc->fieldType == FIELD_CUSTOM ) && ( dataDesc->flags & FTYPEDESC_OUTPUT ) )
			{
				CBaseEntityOutput *pOutput = (CBaseEntityOutput *)((int)this + (int)dataDesc->fieldOffset[0]);
				pOutput->DeleteAllElements();
				/*
				int nConnections = pOutput->NumberOfElements();
				for ( int j = 0; j < nConnections; j++ )
				{

				}
				*/
			}
		}

		dmap = dmap->baseMap;
	}
}
#endif

//-----------------------------------------------------------------------------
// Purpose: Player in our squad killed something
// Input  : *pVictim - Who he killed
//			&info - How they died
//-----------------------------------------------------------------------------
void CNPC_PlayerCompanion::OnPlayerKilledOther( CBaseEntity *pVictim, const CTakeDamageInfo &info )
{
	// filter everything that comes in here that isn't an NPC
	CAI_BaseNPC *pCombatVictim = dynamic_cast<CAI_BaseNPC *>( pVictim );
	if ( !pCombatVictim )
	{
		return;
	}

	CBaseEntity *pInflictor = info.GetInflictor();
#ifdef MAPBASE
	AI_CriteriaSet modifiers;
#else
	int		iNumBarrels = 0;
	int		iConsecutivePlayerKills = 0;
	bool	bPuntedGrenade = false;
	bool	bVictimWasEnemy = false;
	bool	bVictimWasMob = false;
	bool	bVictimWasAttacker = false;
	bool	bHeadshot = false;
	bool	bOneShot = false;
#endif

	if ( dynamic_cast<CBreakableProp *>( pInflictor ) && ( info.GetDamageType() & DMG_BLAST ) )
	{
		// if a barrel explodes that was initiated by the player within a few seconds of the previous one,
		// increment a counter to keep track of how many have exploded in a row.
		if ( gpGlobals->curtime - m_fLastBarrelExploded >= MAX_TIME_BETWEEN_BARRELS_EXPLODING )
		{
			m_iNumConsecutiveBarrelsExploded = 0;
		}
		m_iNumConsecutiveBarrelsExploded++;
		m_fLastBarrelExploded = gpGlobals->curtime;

#ifdef MAPBASE
		modifiers.AppendCriteria( "num_barrels", UTIL_VarArgs("%i", m_iNumConsecutiveBarrelsExploded) );
#else
		iNumBarrels = m_iNumConsecutiveBarrelsExploded;
#endif
	}
	else
	{
		// if player kills an NPC within a few seconds of the previous kill,
		// increment a counter to keep track of how many he's killed in a row.
		if ( gpGlobals->curtime - m_fLastPlayerKill >= MAX_TIME_BETWEEN_CONSECUTIVE_PLAYER_KILLS )
		{
			m_iNumConsecutivePlayerKills = 0;
		}
		m_iNumConsecutivePlayerKills++;
		m_fLastPlayerKill = gpGlobals->curtime;
#ifdef MAPBASE
		modifiers.AppendCriteria( "consecutive_player_kills", UTIL_VarArgs("%i", m_iNumConsecutivePlayerKills) );
#else
		iConsecutivePlayerKills = m_iNumConsecutivePlayerKills;
#endif
	}

	// don't comment on kills when she can't see the victim
	if ( !FVisible( pVictim ) )
	{
		return;
	}

	// check if the player killed an enemy by punting a grenade
#ifdef MAPBASE
	modifiers.AppendCriteria( "punted_grenade", ( pInflictor && Fraggrenade_WasPunted( pInflictor ) && Fraggrenade_WasCreatedByCombine( pInflictor ) ) ? "1" : "0" );
#else
	if ( pInflictor && Fraggrenade_WasPunted( pInflictor ) && Fraggrenade_WasCreatedByCombine( pInflictor ) )
	{
		bPuntedGrenade = true;
	}
#endif

	// check if the victim was Alyx's enemy
#ifdef MAPBASE
	modifiers.AppendCriteria( "victim_was_enemy", GetEnemy() == pVictim ? "1" : "0" );
#else
	if ( GetEnemy() == pVictim )
	{
		bVictimWasEnemy = true;
	}
#endif

	AI_EnemyInfo_t *pEMemory = GetEnemies()->Find( pVictim );
	if ( pEMemory != NULL ) 
	{
		// was Alyx being mobbed by this enemy?
#ifdef MAPBASE
		modifiers.AppendCriteria( "victim_was_mob", pEMemory->bMobbedMe ? "1" : "0" );
		modifiers.AppendCriteria( "victim_was_attacker", pEMemory->timeLastReceivedDamageFrom > 0 ? "1" : "0" );
#else
		bVictimWasMob = pEMemory->bMobbedMe;

		// has Alyx recieved damage from this enemy?
		if ( pEMemory->timeLastReceivedDamageFrom > 0 ) {
			bVictimWasAttacker = true;
		}
#endif
	}
#ifdef MAPBASE
	else
	{
		modifiers.AppendCriteria( "victim_was_mob", "0" );
		modifiers.AppendCriteria( "victim_was_attacker", "0" );
	}
#endif

#ifdef MAPBASE
	modifiers.AppendCriteria( "headshot", ((pCombatVictim->LastHitGroup() == HITGROUP_HEAD) && (info.GetDamageType() & DMG_BULLET)) ? "1" : "0" );
	modifiers.AppendCriteria( "oneshot", ((pCombatVictim->GetDamageCount() == 1) && (info.GetDamageType() & DMG_BULLET)) ? "1" : "0" );
#else
	// Was it a headshot?
	if ( ( pCombatVictim->LastHitGroup() == HITGROUP_HEAD ) && ( info.GetDamageType() & DMG_BULLET ) )
	{
		bHeadshot = true;
	}

	// Did the player kill the enemy with 1 shot?
	if ( ( pCombatVictim->GetDamageCount() == 1 ) && ( info.GetDamageType() & DMG_BULLET ) )
	{
		bOneShot = true;
	}
#endif

#ifdef MAPBASE
	ModifyOrAppendEnemyCriteria(modifiers, pVictim);
#else
	// set up the speech modifiers
	CFmtStrN<512> modifiers( "num_barrels:%d,distancetoplayerenemy:%f,playerAmmo:%s,consecutive_player_kills:%d,"
		"punted_grenade:%d,victim_was_enemy:%d,victim_was_mob:%d,victim_was_attacker:%d,headshot:%d,oneshot:%d",
		iNumBarrels, EnemyDistance( pVictim ), info.GetAmmoName(), iConsecutivePlayerKills,
		bPuntedGrenade, bVictimWasEnemy, bVictimWasMob, bVictimWasAttacker, bHeadshot, bOneShot );
#endif

	SpeakIfAllowed( TLK_PLAYER_KILLED_NPC, modifiers );

	BaseClass::OnPlayerKilledOther( pVictim, info );
}

#ifdef MAPBASE
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_PlayerCompanion::Event_KilledOther( CBaseEntity *pVictim, const CTakeDamageInfo &info )
{
	if ( pVictim )
	{
		if (pVictim->IsPlayer() || (pVictim->IsNPC() &&
			( pVictim->MyNPCPointer()->GetLastPlayerDamageTime() == 0 ||
			  gpGlobals->curtime - pVictim->MyNPCPointer()->GetLastPlayerDamageTime() > 5 )) )
		{
			AI_CriteriaSet modifiers;

			AI_EnemyInfo_t *pEMemory = GetEnemies()->Find( pVictim );
			if ( pEMemory != NULL ) 
			{
				modifiers.AppendCriteria( "victim_was_mob", pEMemory->bMobbedMe ? "1" : "0" );
				modifiers.AppendCriteria( "victim_was_attacker", pEMemory->timeLastReceivedDamageFrom > 0 ? "1" : "0" );
			}
			else
			{
				modifiers.AppendCriteria( "victim_was_mob", "0" );
				modifiers.AppendCriteria( "victim_was_attacker", "0" );
			}

			CBaseCombatCharacter *pCombatVictim = pVictim->MyCombatCharacterPointer();
			if (pCombatVictim)
			{
				modifiers.AppendCriteria( "headshot", ((pCombatVictim->LastHitGroup() == HITGROUP_HEAD) && (info.GetDamageType() & DMG_BULLET)) ? "1" : "0" );
				modifiers.AppendCriteria( "oneshot", ((pCombatVictim->GetDamageCount() == 1) && (info.GetDamageType() & DMG_BULLET)) ? "1" : "0" );
			}
			else
			{
				modifiers.AppendCriteria( "headshot", "0" );
				modifiers.AppendCriteria( "oneshot", "0" );
			}

			SetPotentialSpeechTarget( pVictim );
			SetSpeechTarget( pVictim );
			SpeakIfAllowed( TLK_ENEMY_DEAD, modifiers );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Handles stuff ported from Alyx.
// 
// For some reason, I thought Alyx's mobbed AI was used to measure enemy count for criteria stuff, which I wanted citizens to use.
// Now that I realize enemy counting for criteria is elsewhere and this is used for just mobbing in general, I deactivated it
// since it would barely be used and I don't know what kind of an impact it has on performance.
// 
// If you want to use it, feel free to re-activate.
//-----------------------------------------------------------------------------
void CNPC_PlayerCompanion::DoCustomCombatAI( void )
{
	/*
	#define COMPANION_MIN_MOB_DIST_SQR Square(120)		// Any enemy closer than this adds to the 'mob'
	#define COMPANION_MIN_CONSIDER_DIST	Square(1200)	// Only enemies within this range are counted and considered to generate AI speech

	AIEnemiesIter_t iter;

	float visibleEnemiesScore = 0.0f;
	float closeEnemiesScore = 0.0f;

	for ( AI_EnemyInfo_t *pEMemory = GetEnemies()->GetFirst(&iter); pEMemory != NULL; pEMemory = GetEnemies()->GetNext(&iter) )
	{
		if ( IRelationType( pEMemory->hEnemy ) != D_NU && IRelationType( pEMemory->hEnemy ) != D_LI && pEMemory->hEnemy->GetAbsOrigin().DistToSqr(GetAbsOrigin()) <= COMPANION_MIN_CONSIDER_DIST )
		{
			if( pEMemory->hEnemy && pEMemory->hEnemy->IsAlive() && gpGlobals->curtime - pEMemory->timeLastSeen <= 0.5f && pEMemory->hEnemy->Classify() != CLASS_BULLSEYE )
			{
				if( pEMemory->hEnemy->GetAbsOrigin().DistToSqr(GetAbsOrigin()) <= COMPANION_MIN_MOB_DIST_SQR )
				{
					closeEnemiesScore += 1.0f;
				}
				else
				{
					visibleEnemiesScore += 1.0f;
				}
			}
		}
	}

	if( closeEnemiesScore > 2 )
	{
		SetCondition( COND_MOBBED_BY_ENEMIES );

		// mark anyone in the mob as having mobbed me
		for ( AI_EnemyInfo_t *pEMemory = GetEnemies()->GetFirst(&iter); pEMemory != NULL; pEMemory = GetEnemies()->GetNext(&iter) )
		{
			if ( pEMemory->bMobbedMe )
				continue;

			if ( IRelationType( pEMemory->hEnemy ) != D_NU && IRelationType( pEMemory->hEnemy ) != D_LI && pEMemory->hEnemy->GetAbsOrigin().DistToSqr(GetAbsOrigin()) <= COMPANION_MIN_CONSIDER_DIST )
			{
				if( pEMemory->hEnemy && pEMemory->hEnemy->IsAlive() && gpGlobals->curtime - pEMemory->timeLastSeen <= 0.5f && pEMemory->hEnemy->Classify() != CLASS_BULLSEYE )
				{
					if( pEMemory->hEnemy->GetAbsOrigin().DistToSqr(GetAbsOrigin()) <= COMPANION_MIN_MOB_DIST_SQR )
					{
						pEMemory->bMobbedMe = true;
					}
				}
			}
		}
	}
	else
	{
		ClearCondition( COND_MOBBED_BY_ENEMIES );
	}

	// Say a combat thing
	if( HasCondition( COND_MOBBED_BY_ENEMIES ) )
	{
		SpeakIfAllowed( TLK_MOBBED );
	}
	else if( visibleEnemiesScore > 4 )
	{
		SpeakIfAllowed( TLK_MANY_ENEMIES );
	}
	*/
}
#endif

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CNPC_PlayerCompanion::IsNavigationUrgent( void )
{
	bool bBase = BaseClass::IsNavigationUrgent();

	// Consider follow & assault behaviour urgent
	if ( !bBase && (m_FollowBehavior.IsActive() || ( m_AssaultBehavior.IsRunning() && m_AssaultBehavior.IsUrgent() )) && Classify() == CLASS_PLAYER_ALLY_VITAL ) 
	{
		// But only if the blocker isn't the player, and isn't a physics object that's still moving
		CBaseEntity *pBlocker = GetNavigator()->GetBlockingEntity();
		if ( pBlocker && !pBlocker->IsPlayer() )
		{
			IPhysicsObject *pPhysObject = pBlocker->VPhysicsGetObject();
			if ( pPhysObject && !pPhysObject->IsAsleep() )
				return false;
			if ( pBlocker->IsNPC() )
				return false;
		}

		// If we're within the player's viewcone, then don't teleport.

		// This test was made more general because previous iterations had cases where characters
		// could not see the player but the player could in fact see them.  Now the NPC's facing is
		// irrelevant and the player's viewcone is more authorative. -- jdw

		CBasePlayer *pLocalPlayer = AI_GetSinglePlayer();
		if ( pLocalPlayer->FInViewCone( EyePosition() ) )
			return false;

		return true;
	}

	return bBase;
}

//-----------------------------------------------------------------------------
//
// Schedules
//
//-----------------------------------------------------------------------------

AI_BEGIN_CUSTOM_NPC( player_companion_base, CNPC_PlayerCompanion )

	// AI Interaction for being hit by a physics object
	DECLARE_INTERACTION(g_interactionHitByPlayerThrownPhysObj)
	DECLARE_INTERACTION(g_interactionPlayerPuntedHeavyObject)

	DECLARE_CONDITION( COND_PC_HURTBYFIRE )
	DECLARE_CONDITION( COND_PC_SAFE_FROM_MORTAR )
	DECLARE_CONDITION( COND_PC_BECOMING_PASSENGER )

	DECLARE_TASK( TASK_PC_WAITOUT_MORTAR )
	DECLARE_TASK( TASK_PC_GET_PATH_OFF_COMPANION )
#ifdef MAPBASE
	DECLARE_TASK( TASK_PC_PLAY_SEQUENCE_FACE_ALTFIRE_TARGET )
	DECLARE_TASK( TASK_PC_GET_PATH_TO_FORCED_GREN_LOS )
	DECLARE_TASK( TASK_PC_DEFER_SQUAD_GRENADES )
	DECLARE_TASK( TASK_PC_FACE_TOSS_DIR )
#endif

	DECLARE_ANIMEVENT( AE_COMPANION_PRODUCE_FLARE )
	DECLARE_ANIMEVENT( AE_COMPANION_LIGHT_FLARE )
	DECLARE_ANIMEVENT( AE_COMPANION_RELEASE_FLARE )
#ifdef MAPBASE
	DECLARE_ANIMEVENT( COMBINE_AE_BEGIN_ALTFIRE )
	DECLARE_ANIMEVENT( COMBINE_AE_ALTFIRE )
#endif

	//=========================================================
	// > TakeCoverFromBestSound
	//
	//	Find cover and move towards it, but only do so for a short
	//  time. This is appropriate when the dangerous item is going
	//  to detonate very soon. This way our NPC doesn't run a great
	//  distance from an object that explodes shortly after the NPC
	//  gets underway.
	//=========================================================
	DEFINE_SCHEDULE
	(
		SCHED_PC_MOVE_TOWARDS_COVER_FROM_BEST_SOUND,

		"	Tasks"
		"		 TASK_SET_FAIL_SCHEDULE				SCHEDULE:SCHED_FLEE_FROM_BEST_SOUND"
		"		 TASK_STOP_MOVING					0"
		"		 TASK_SET_TOLERANCE_DISTANCE		24"
		"		 TASK_STORE_BESTSOUND_REACTORIGIN_IN_SAVEPOSITION	0"
		"		 TASK_FIND_COVER_FROM_BEST_SOUND	0"
		"		 TASK_RUN_PATH_TIMED				1.0"
		"		 TASK_STOP_MOVING					0"
		"		 TASK_FACE_SAVEPOSITION				0"
		"		 TASK_SET_ACTIVITY					ACTIVITY:ACT_IDLE"	// Translated to cover
		""
		"	Interrupts"
		"		COND_PC_SAFE_FROM_MORTAR"
	)

	DEFINE_SCHEDULE
	(
	SCHED_PC_TAKE_COVER_FROM_BEST_SOUND,

	"	Tasks"
	"		 TASK_SET_FAIL_SCHEDULE								SCHEDULE:SCHED_FLEE_FROM_BEST_SOUND"
	"		 TASK_STOP_MOVING									0"
	"		 TASK_SET_TOLERANCE_DISTANCE						24"
	"		 TASK_STORE_BESTSOUND_REACTORIGIN_IN_SAVEPOSITION	0"
	"		 TASK_FIND_COVER_FROM_BEST_SOUND					0"
	"		 TASK_RUN_PATH										0"
	"		 TASK_WAIT_FOR_MOVEMENT								0"
	"		 TASK_STOP_MOVING									0"
	"		 TASK_FACE_SAVEPOSITION								0"
	"		 TASK_SET_ACTIVITY									ACTIVITY:ACT_IDLE"	// Translated to cover
	""
	"	Interrupts"
	"		COND_NEW_ENEMY"
	"		COND_PC_SAFE_FROM_MORTAR"
	)

	DEFINE_SCHEDULE	
	(
		SCHED_PC_COWER,
		  
		"	Tasks"
		"		TASK_WAIT_RANDOM			0.1"
		"		TASK_SET_ACTIVITY			ACTIVITY:ACT_COWER"
		"		TASK_PC_WAITOUT_MORTAR		0"
		"		TASK_WAIT					0.1"	
		"		TASK_WAIT_RANDOM			0.5"	
		""
		"	Interrupts"
		"		"
	)

	//=========================================================
	//
	//=========================================================
	DEFINE_SCHEDULE
	(
		SCHED_PC_FLEE_FROM_BEST_SOUND,

		"	Tasks"
		"		 TASK_SET_FAIL_SCHEDULE				SCHEDULE:SCHED_COWER"
		"		 TASK_GET_PATH_AWAY_FROM_BEST_SOUND	600"
		"		 TASK_RUN_PATH_TIMED				1.5"
		"		 TASK_STOP_MOVING					0"
		"		 TASK_TURN_LEFT						179"
		""
		"	Interrupts"
		"		COND_NEW_ENEMY"
		"		COND_PC_SAFE_FROM_MORTAR"
	)

	//=========================================================
	DEFINE_SCHEDULE
	(
		SCHED_PC_FAIL_TAKE_COVER_TURRET,

		"	Tasks"
		"		 TASK_SET_FAIL_SCHEDULE				SCHEDULE:SCHED_COWER"
		"		 TASK_STOP_MOVING					0"
		"		 TASK_MOVE_AWAY_PATH				600"
		"		 TASK_RUN_PATH_FLEE					100"
		"		 TASK_STOP_MOVING					0"
		"		 TASK_TURN_LEFT						179"
		""
		"	Interrupts"
		"		COND_NEW_ENEMY"
	)

	//=========================================================
	DEFINE_SCHEDULE
	(
		SCHED_PC_FAKEOUT_MORTAR,

		"	Tasks"
		"		TASK_MOVE_AWAY_PATH						300"
		"		TASK_RUN_PATH							0"
		"		TASK_WAIT_FOR_MOVEMENT					0"
		""
		"	Interrupts"
		"		COND_HEAR_DANGER"
	)

	//=========================================================
	DEFINE_SCHEDULE
	(
		SCHED_PC_GET_OFF_COMPANION,

		"	Tasks"
		"		TASK_PC_GET_PATH_OFF_COMPANION				0"
		"		TASK_RUN_PATH							0"
		"		TASK_WAIT_FOR_MOVEMENT					0"
		""
		"	Interrupts"
		""
	)

#ifdef COMPANION_MELEE_ATTACK
	DEFINE_SCHEDULE
	(
		SCHED_PC_MELEE_AND_MOVE_AWAY,

		"	Tasks"
		"		TASK_STOP_MOVING		0"
		"		TASK_FACE_ENEMY			0"
		"		TASK_ANNOUNCE_ATTACK	1"	// 1 = primary attack
		"		TASK_MELEE_ATTACK1		0"
		"		TASK_SET_SCHEDULE			SCHEDULE:SCHED_MOVE_AWAY_FROM_ENEMY"
		""
		"	Interrupts"
		"		COND_NEW_ENEMY"
		"		COND_ENEMY_DEAD"
		//"		COND_LIGHT_DAMAGE"
		"		COND_HEAVY_DAMAGE"
		"		COND_ENEMY_OCCLUDED"
	)
#endif

#ifdef MAPBASE
	//=========================================================
	// AR2 Alt Fire Attack
	//=========================================================
	DEFINE_SCHEDULE
	(
	SCHED_PC_AR2_ALTFIRE,

	"	Tasks"
	"		TASK_STOP_MOVING									0"
	"		TASK_ANNOUNCE_ATTACK								1"
	"		TASK_PC_PLAY_SEQUENCE_FACE_ALTFIRE_TARGET		ACTIVITY:ACT_COMBINE_AR2_ALTFIRE"
	""
	"	Interrupts"
	"		COND_TOO_CLOSE_TO_ATTACK"
	)

	//=========================================================
	// Move to LOS of the mapmaker's forced grenade throw target
	//=========================================================
	DEFINE_SCHEDULE
	(
	SCHED_PC_MOVE_TO_FORCED_GREN_LOS,

	"	Tasks "
	"		TASK_SET_TOLERANCE_DISTANCE					48"
	"		TASK_PC_GET_PATH_TO_FORCED_GREN_LOS			0"
	"		TASK_SPEAK_SENTENCE							1"
	"		TASK_RUN_PATH								0"
	"		TASK_WAIT_FOR_MOVEMENT						0"
	"	"
	"	Interrupts "
	"		COND_NEW_ENEMY"
	"		COND_ENEMY_DEAD"
	"		COND_CAN_MELEE_ATTACK1"
	"		COND_CAN_MELEE_ATTACK2"
	"		COND_HEAR_DANGER"
	"		COND_HEAR_MOVE_AWAY"
	"		COND_HEAVY_DAMAGE"
	)

	//=========================================================
	// Mapmaker forced grenade throw
	//=========================================================
	DEFINE_SCHEDULE
	(
	SCHED_PC_FORCED_GRENADE_THROW,

	"	Tasks"
	"		TASK_STOP_MOVING					0"
	"		TASK_PC_FACE_TOSS_DIR				0"
	"		TASK_ANNOUNCE_ATTACK				2"	// 2 = grenade
	"		TASK_PLAY_SEQUENCE					ACTIVITY:ACT_RANGE_ATTACK2"
	"		TASK_PC_DEFER_SQUAD_GRENADES	0"
	""
	"	Interrupts"
	)

	//=========================================================
	// 	SCHED_PC_RANGE_ATTACK2	
	//
	//	secondary range attack. Overriden because base class stops attacking when the enemy is occluded.
	//	combines's grenade toss requires the enemy be occluded.
	//=========================================================
	DEFINE_SCHEDULE
	(
	SCHED_PC_RANGE_ATTACK2,

	"	Tasks"
	"		TASK_STOP_MOVING					0"
	"		TASK_PC_FACE_TOSS_DIR			0"
	"		TASK_ANNOUNCE_ATTACK				2"	// 2 = grenade
	"		TASK_PLAY_SEQUENCE					ACTIVITY:ACT_RANGE_ATTACK2"
	"		TASK_PC_DEFER_SQUAD_GRENADES	0"
	"		TASK_SET_SCHEDULE					SCHEDULE:SCHED_HIDE_AND_RELOAD"	// don't run immediately after throwing grenade.
	""
	"	Interrupts"
	)
#endif

AI_END_CUSTOM_NPC()


//
// Special movement overrides for player companions
//

#define NUM_OVERRIDE_MOVE_CLASSNAMES	4

class COverrideMoveCache : public IEntityListener
{
public:

	void LevelInitPreEntity( void )
	{ 
		CacheClassnames();
		gEntList.AddListenerEntity( this );
		Clear(); 
	}
	void LevelShutdownPostEntity( void  )
	{
		gEntList.RemoveListenerEntity( this );
		Clear();
	}

	inline void Clear( void )
	{ 
		m_Cache.Purge(); 
	}

	inline bool MatchesCriteria( CBaseEntity *pEntity )
	{
		for ( int i = 0; i < NUM_OVERRIDE_MOVE_CLASSNAMES; i++ )
		{
			if ( pEntity->m_iClassname == m_Classname[i] )
				return true;
		}

		return false;
	}

	virtual void OnEntitySpawned( CBaseEntity *pEntity )
	{
		if ( MatchesCriteria( pEntity ) )
		{
			m_Cache.AddToTail( pEntity );
		}
	};

	virtual void OnEntityDeleted( CBaseEntity *pEntity )
	{
		if ( !m_Cache.Count() )
			return;

		if ( MatchesCriteria( pEntity ) )
		{
			m_Cache.FindAndRemove( pEntity );
		}
	};

	CBaseEntity *FindTargetsInRadius( CBaseEntity *pFirstEntity, const Vector &vecOrigin, float flRadius )
	{
		if ( !m_Cache.Count() )
			return NULL;

		int nIndex = m_Cache.InvalidIndex();

		// If we're starting with an entity, start there and move past it
		if ( pFirstEntity != NULL ) 
		{
			nIndex = m_Cache.Find( pFirstEntity );
			nIndex = m_Cache.Next( nIndex );
			if ( nIndex == m_Cache.InvalidIndex() )
				return NULL;
		}
		else 
		{
			nIndex = m_Cache.Head();
		}

		CBaseEntity *pTarget = NULL;
		const float flRadiusSqr = Square( flRadius );

		// Look through each cached target, looking for one in our range
		while ( nIndex != m_Cache.InvalidIndex() )
		{
			pTarget = m_Cache[nIndex];
			if ( pTarget && ( pTarget->GetAbsOrigin() - vecOrigin ).LengthSqr() < flRadiusSqr )
				return pTarget;

			nIndex = m_Cache.Next( nIndex );
		}

		return NULL;
	}

	void ForceRepopulateList( void )
	{
		Clear();
		CacheClassnames();

		CBaseEntity *pEnt = gEntList.FirstEnt();
		while( pEnt )
		{
			if( MatchesCriteria( pEnt ) )
			{
				m_Cache.AddToTail( pEnt );
			}

			pEnt = gEntList.NextEnt( pEnt );
		}
	}

private:
	inline void CacheClassnames( void )
	{
		m_Classname[0] = AllocPooledString( "env_fire" );
		m_Classname[1] = AllocPooledString( "combine_mine" );
		m_Classname[2] = AllocPooledString( "npc_turret_floor" );
		m_Classname[3] = AllocPooledString( "entityflame" );
	}

	CUtlLinkedList<EHANDLE>	m_Cache;
	string_t				m_Classname[NUM_OVERRIDE_MOVE_CLASSNAMES];
};

// Singleton for access
COverrideMoveCache g_OverrideMoveCache;
COverrideMoveCache *OverrideMoveCache( void ) { return &g_OverrideMoveCache; }

CBaseEntity *OverrideMoveCache_FindTargetsInRadius( CBaseEntity *pFirstEntity, const Vector &vecOrigin, float flRadius )
{
	return g_OverrideMoveCache.FindTargetsInRadius( pFirstEntity, vecOrigin, flRadius );
}

void OverrideMoveCache_ForceRepopulateList( void )
{
	g_OverrideMoveCache.ForceRepopulateList();
}

void OverrideMoveCache_LevelInitPreEntity( void )
{
	g_OverrideMoveCache.LevelInitPreEntity();
}

void OverrideMoveCache_LevelShutdownPostEntity( void )
{
	g_OverrideMoveCache.LevelShutdownPostEntity();
}

