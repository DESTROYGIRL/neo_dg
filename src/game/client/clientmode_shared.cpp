//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Normal HUD mode
//
// $Workfile:     $
// $Date:         $
// $NoKeywords: $
//=============================================================================//


#include "cbase.h"
#include "clientmode_shared.h"
#include "iinput.h"
#include "view_shared.h"
#include "iviewrender.h"
#include "hud_basechat.h"
#include "weapon_selection.h"
#include <vgui/IVGui.h>
#include <vgui/Cursor.h>
#include <vgui/IPanel.h>
#include <vgui/IInput.h>
#include "engine/IEngineSound.h"
#include <KeyValues.h>
#include <vgui_controls/AnimationController.h>
#include "vgui_int.h"
#include "hud_macros.h"
#include "hltvcamera.h"
#include "particlemgr.h"
#include "c_vguiscreen.h"
#include "c_team.h"
#include "c_rumble.h"
#include "fmtstr.h"
#include "achievementmgr.h"
#include "c_playerresource.h"
#include "cam_thirdperson.h"
#include <vgui/ILocalize.h>
#include "hud_vote.h"
#include "ienginevgui.h"
#include "sourcevr/isourcevirtualreality.h"
#if defined( _X360 )
#include "xbox/xbox_console.h"
#endif

#if defined( REPLAY_ENABLED )
#include "replay/replaycamera.h"
#include "replay/ireplaysystem.h"
#include "replay/iclientreplaycontext.h"
#include "replay/ireplaymanager.h"
#include "replay/replay.h"
#include "replay/ienginereplay.h"
#include "replay/vgui/replayreminderpanel.h"
#include "replay/vgui/replaymessagepanel.h"
#include "econ/econ_controls.h"
#include "econ/confirm_dialog.h"
extern IClientReplayContext *g_pClientReplayContext;
extern ConVar replay_rendersetting_renderglow;
#endif

#if defined USES_ECON_ITEMS
#include "econ_item_view.h"
#endif

#if defined( TF_CLIENT_DLL )
#include "tf_gc_client.h"
#include "c_tf_player.h"
#include "econ_item_description.h"
#include "c_tf_team.h"
#endif

#ifdef NEO
#include "c_neo_player.h"
#include <GameUI/IGameUI.h>
#include "ui/neo_loading.h"
#include "neo_gamerules.h"
#endif

#ifdef GLOWS_ENABLE
#include "clienteffectprecachesystem.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#ifdef GLOWS_ENABLE
CLIENTEFFECT_REGISTER_BEGIN(PrecachePostProcessingEffectsGlow)
CLIENTEFFECT_MATERIAL("dev/glow_color")
CLIENTEFFECT_MATERIAL("dev/halo_add_to_screen")
CLIENTEFFECT_MATERIAL("dev/halo_add_to_screen_outline")
CLIENTEFFECT_REGISTER_END_CONDITIONAL(engine->GetDXSupportLevel() >= 90)
#endif
#define ACHIEVEMENT_ANNOUNCEMENT_MIN_TIME 10

class CHudWeaponSelection;
class CHudChat;
class CHudVote;

#ifdef NEO
static CDllDemandLoader g_gameUI("GameUI");
extern ConVar cl_neo_streamermode;
#endif

static vgui::HContext s_hVGuiContext = DEFAULT_VGUI_CONTEXT;

ConVar cl_drawhud( "cl_drawhud", "1", FCVAR_NONE, "Enable the rendering of the hud" );
ConVar hud_takesshots( "hud_takesshots", "0", FCVAR_CLIENTDLL | FCVAR_ARCHIVE, "Auto-save a scoreboard screenshot at the end of a map." );
ConVar hud_freezecamhide( "hud_freezecamhide", "0", FCVAR_CLIENTDLL | FCVAR_ARCHIVE, "Hide the HUD during freeze-cam" );
ConVar cl_show_num_particle_systems( "cl_show_num_particle_systems", "0", FCVAR_CLIENTDLL, "Display the number of active particle systems." );

extern ConVar v_viewmodel_fov;
extern ConVar voice_modenable;
extern ConVar cl_enable_text_chat;

extern bool IsInCommentaryMode( void );
extern const char* GetWearLocalizationString( float flWear );

CON_COMMAND( cl_reload_localization_files, "Reloads all localization files" )
{
	g_pVGuiLocalize->ReloadLocalizationFiles();
}

#ifdef NEO
CNeoLoading *g_pNeoLoading;
#endif

#ifdef VOICE_VOX_ENABLE
void VoxCallback( IConVar *var, const char *oldString, float oldFloat )
{
	if ( engine && engine->IsConnected() )
	{
		const ConVarRef voice_vox( var );
		if ( voice_vox.GetBool() && voice_modenable.GetBool() )
		{
			engine->ClientCmd_Unrestricted( "voicerecord_toggle on\n" );
		}
		else
		{
			engine->ClientCmd_Unrestricted( "voicerecord_toggle off\n" );
		}
	}
}
ConVar voice_vox( "voice_vox", "0", FCVAR_ARCHIVE, "Voice chat uses a vox-style always on", true, 0, true, 1, VoxCallback );

// --------------------------------------------------------------------------------- //
// CVoxManager.
// --------------------------------------------------------------------------------- //
class CVoxManager : public CAutoGameSystem
{
public:
	CVoxManager() : CAutoGameSystem( "VoxManager" )
	{
	}

	virtual void LevelInitPostEntity( void )
	{
		if ( voice_vox.GetBool() && voice_modenable.GetBool() )
		{
			engine->ClientCmd_Unrestricted( "voicerecord_toggle on\n" );
		}
	}

	virtual void LevelShutdownPreEntity( void )
	{
		if ( voice_vox.GetBool() )
		{
			engine->ClientCmd_Unrestricted( "voicerecord_toggle off\n" );
		}
	}
};

static CVoxManager s_VoxManager;
// --------------------------------------------------------------------------------- //
#endif // VOICE_VOX_ENABLE

CON_COMMAND( hud_reloadscheme, "Reloads hud layout and animation scripts." )
{
	ClientModeShared *mode = ( ClientModeShared * )GetClientModeNormal();
	if ( !mode )
		return;

	mode->ReloadScheme(true);
}

#ifdef _DEBUG
CON_COMMAND_F( crash, "Crash the client. Optional parameter -- type of crash:\n 0: read from NULL\n 1: write to NULL\n 2: DmCrashDump() (xbox360 only)", FCVAR_CHEAT )
{
	int crashtype = 0;
	int dummy;
	if ( args.ArgC() > 1 )
	{
		 crashtype = Q_atoi( args[1] );
	}
	switch (crashtype)
	{
		case 0:
			dummy = *((int *) NULL);
			Msg("Crashed! %d\n", dummy); // keeps dummy from optimizing out
			break;
		case 1:
			*((int *)NULL) = 42;
			break;
#if defined( _X360 )
		case 2:
			XBX_CrashDump(false);
			break;
#endif
		default:
			Msg("Unknown variety of crash. You have now failed to crash. I hope you're happy.\n");
			break;
	}
}
#endif // _DEBUG

static void __MsgFunc_Rumble( bf_read &msg )
{
	unsigned char waveformIndex;
	unsigned char rumbleData;
	unsigned char rumbleFlags;

	waveformIndex = msg.ReadByte();
	rumbleData = msg.ReadByte();
	rumbleFlags = msg.ReadByte();

	RumbleEffect( waveformIndex, rumbleData, rumbleFlags );
}

static void __MsgFunc_VGUIMenu( bf_read &msg )
{
	char panelname[2048]; 
	
	msg.ReadString( panelname, sizeof(panelname) );

	bool  bShow = msg.ReadByte()!=0;
	
	IViewPortPanel *viewport = gViewPortInterface->FindPanelByName( panelname );

	if ( !viewport )
	{
		// DevMsg("VGUIMenu: couldn't find panel '%s'.\n", panelname );
		return;
	}

	int count = msg.ReadByte();

	if ( count > 0 )
	{
		KeyValues *keys = new KeyValues("data");
		//Msg( "MsgFunc_VGUIMenu:\n" );

		for ( int i=0; i<count; i++)
		{
			char name[255];
			char data[255];

			msg.ReadString( name, sizeof(name) );
			msg.ReadString( data, sizeof(data) );
			//Msg( "  %s <- '%s'\n", name, data );

			keys->SetString( name, data );
		}

		// !KLUDGE! Whitelist of URL protocols formats for MOTD
		if (
			!V_stricmp( panelname, PANEL_INFO ) // MOTD
			&& keys->GetInt( "type", 0 ) == 2 // URL message type
		) {
			const char *pszURL = keys->GetString( "msg", "" );
			if ( Q_strncmp( pszURL, "http://", 7 ) != 0 && Q_strncmp( pszURL, "https://", 8 ) != 0 && Q_stricmp( pszURL, "about:blank" ) != 0 )
			{
				Warning( "Blocking MOTD URL '%s'; must begin with 'http://' or 'https://' or be about:blank\n", pszURL );
				keys->deleteThis();
				return;
			}
		}

		viewport->SetData( keys );

		keys->deleteThis();
	}

	// is the server telling us to show the scoreboard (at the end of a map)?
	if ( Q_stricmp( panelname, "scores" ) == 0 )
	{
		if ( hud_takesshots.GetBool() == true )
		{
			gHUD.SetScreenShotTime( gpGlobals->curtime + 1.0 ); // take a screenshot in 1 second
		}

		IGameEvent *event = gameeventmanager->CreateEvent( "ds_screenshot" );
		if ( event )
		{
			event->SetFloat( "delay", 0.5f );
			gameeventmanager->FireEventClientSide( event );
		}
	}

	// is the server trying to show an MOTD panel? Check that it's allowed right now.
	ClientModeShared *mode = ( ClientModeShared * )GetClientModeNormal();
	if ( Q_stricmp( panelname, PANEL_INFO ) == 0 && mode )
	{
		if ( !mode->IsInfoPanelAllowed() )
		{
			return;
		}
		else
		{
			mode->InfoPanelDisplayed();
		}
	}

	gViewPortInterface->ShowPanel( viewport, bShow );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
ClientModeShared::ClientModeShared()
{
	m_pViewport = NULL;
	m_pChatElement = NULL;
	m_pWeaponSelection = NULL;
	m_nRootSize[ 0 ] = m_nRootSize[ 1 ] = -1;

#ifdef NEO
	g_pNeoLoading = nullptr;
#endif

#if defined( REPLAY_ENABLED )
	m_pReplayReminderPanel = NULL;
	m_flReplayStartRecordTime = 0.0f;
	m_flReplayStopRecordTime = 0.0f;
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
ClientModeShared::~ClientModeShared()
{
	delete m_pViewport; 
}

void ClientModeShared::ReloadScheme( bool flushLowLevel )
{
	// Invalidate the global cache first.
	if (flushLowLevel)
	{
		KeyValuesSystem()->InvalidateCache();
	}

	BuildGroup::ClearResFileCache();

	m_pViewport->ReloadScheme( "resource/ClientScheme.res" );
}


//----------------------------------------------------------------------------
// Purpose: Let the client mode set some vgui conditions
//-----------------------------------------------------------------------------
void	ClientModeShared::ComputeVguiResConditions( KeyValues *pkvConditions ) 
{
	if ( UseVR() )
	{
		pkvConditions->FindKey( "if_vr", true );
	}
}



//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void ClientModeShared::Init()
{
	m_pChatElement = ( CBaseHudChat * )GET_HUDELEMENT( CHudChat );
	Assert( m_pChatElement );

	m_pWeaponSelection = ( CBaseHudWeaponSelection * )GET_HUDELEMENT( CHudWeaponSelection );
	Assert( m_pWeaponSelection );

	KeyValuesAD pConditions( "conditions" );
	ComputeVguiResConditions( pConditions );

	// Derived ClientMode class must make sure m_Viewport is instantiated
	Assert( m_pViewport );
	m_pViewport->LoadControlSettings( "scripts/HudLayout.res", NULL, NULL, pConditions );

#if defined( REPLAY_ENABLED )
 	m_pReplayReminderPanel = GET_HUDELEMENT( CReplayReminderPanel );
 	Assert( m_pReplayReminderPanel );
#endif

	ListenForGameEvent( "player_connect_client" );
	ListenForGameEvent( "player_disconnect" );
	ListenForGameEvent( "player_team" );
	ListenForGameEvent( "server_cvar" );
	ListenForGameEvent( "player_changename" );
#ifdef NEO
	ListenForGameEvent("player_changeneoname");
#endif
	ListenForGameEvent( "teamplay_broadcast_audio" );
	ListenForGameEvent( "achievement_earned" );

#if defined( TF_CLIENT_DLL )
	ListenForGameEvent( "item_found" );
#endif 

#if defined( REPLAY_ENABLED )
	ListenForGameEvent( "replay_startrecord" );
	ListenForGameEvent( "replay_endrecord" );
	ListenForGameEvent( "replay_replaysavailable" );
	ListenForGameEvent( "replay_servererror" );
	ListenForGameEvent( "game_newmap" );
#endif

#ifndef _XBOX
	HLTVCamera()->Init();
#if defined( REPLAY_ENABLED )
	ReplayCamera()->Init();
#endif
#endif

	m_CursorNone = vgui::dc_none;

	HOOK_MESSAGE( VGUIMenu );
	HOOK_MESSAGE( Rumble );

#ifdef NEO
	const bool bCliArgTools = CommandLine()->CheckParm("-tools");
	if (!bCliArgTools)
	{
		if (CreateInterfaceFn gameUIFactory = g_gameUI.GetFactory())
		{
			if (IGameUI *pGameUI = reinterpret_cast<IGameUI *>(gameUIFactory(GAMEUI_INTERFACE_VERSION, nullptr)))
			{
				g_pNeoLoading = new CNeoLoading;
				pGameUI->SetLoadingBackgroundDialog(g_pNeoLoading->GetVPanel());
			}
		}
	}
#endif
}


void ClientModeShared::InitViewport()
{
}


void ClientModeShared::VGui_Shutdown()
{
	delete m_pViewport;
	m_pViewport = NULL;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void ClientModeShared::Shutdown()
{
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : frametime - 
//			*cmd - 
//-----------------------------------------------------------------------------
bool ClientModeShared::CreateMove( float flInputSampleTime, CUserCmd *cmd )
{
	// Let the player override the view.
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if(!pPlayer)
		return true;

	// Let the player at it
	return pPlayer->CreateMove( flInputSampleTime, cmd );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pSetup - 
//-----------------------------------------------------------------------------
void ClientModeShared::OverrideView( CViewSetup *pSetup )
{
	QAngle camAngles;

	// Let the player override the view.
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if(!pPlayer)
		return;

	pPlayer->OverrideView( pSetup );

	if( ::input->CAM_IsThirdPerson() )
	{
		const Vector& cam_ofs = g_ThirdPersonManager.GetCameraOffsetAngles();
		Vector cam_ofs_distance = g_ThirdPersonManager.GetFinalCameraOffset();

		cam_ofs_distance *= g_ThirdPersonManager.GetDistanceFraction();

		camAngles[ PITCH ] = cam_ofs[ PITCH ];
		camAngles[ YAW ] = cam_ofs[ YAW ];
		camAngles[ ROLL ] = 0;

		Vector camForward, camRight, camUp;
		

		if ( g_ThirdPersonManager.IsOverridingThirdPerson() == false )
		{
			engine->GetViewAngles( camAngles );
		}
			
		// get the forward vector
		AngleVectors( camAngles, &camForward, &camRight, &camUp );
	
		VectorMA( pSetup->origin, -cam_ofs_distance[0], camForward, pSetup->origin );
		VectorMA( pSetup->origin, cam_ofs_distance[1], camRight, pSetup->origin );
		VectorMA( pSetup->origin, cam_ofs_distance[2], camUp, pSetup->origin );

		// Override angles from third person camera
		VectorCopy( camAngles, pSetup->angles );
	}
	else if (::input->CAM_IsOrthographic())
	{
		pSetup->m_bOrtho = true;
		float w, h;
		::input->CAM_OrthographicSize( w, h );
		w *= 0.5f;
		h *= 0.5f;
		pSetup->m_OrthoLeft   = -w;
		pSetup->m_OrthoTop    = -h;
		pSetup->m_OrthoRight  = w;
		pSetup->m_OrthoBottom = h;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool ClientModeShared::ShouldDrawEntity(C_BaseEntity *pEnt)
{
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool ClientModeShared::ShouldDrawParticles( )
{
#ifdef TF_CLIENT_DLL
	C_TFPlayer *pTFPlayer = C_TFPlayer::GetLocalTFPlayer();
	if ( pTFPlayer && !pTFPlayer->ShouldPlayerDrawParticles() )
		return false;
#endif // TF_CLIENT_DLL

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Allow weapons to override mouse input (for binoculars)
//-----------------------------------------------------------------------------
void ClientModeShared::OverrideMouseInput( float *x, float *y )
{
	C_BaseCombatWeapon *pWeapon = GetActiveWeapon();
	if ( pWeapon )
	{
		pWeapon->OverrideMouseInput( x, y );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool ClientModeShared::ShouldDrawViewModel()
{
	return true;
}

bool ClientModeShared::ShouldDrawDetailObjects( )
{
	return true;
}


//-----------------------------------------------------------------------------
// Purpose: Returns true if VR mode should black out everything outside the HUD.
//			This is used for things like sniper scopes and full screen UI
//-----------------------------------------------------------------------------
bool ClientModeShared::ShouldBlackoutAroundHUD()
{
	return enginevgui->IsGameUIVisible();
}


//-----------------------------------------------------------------------------
// Purpose: Allows the client mode to override mouse control stuff in sourcevr
//-----------------------------------------------------------------------------
HeadtrackMovementMode_t ClientModeShared::ShouldOverrideHeadtrackControl() 
{
	return HMM_NOOVERRIDE;
}


//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool ClientModeShared::ShouldDrawCrosshair( void )
{
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Don't draw the current view entity if we are using the fake viewmodel instead
//-----------------------------------------------------------------------------
bool ClientModeShared::ShouldDrawLocalPlayer( C_BasePlayer *pPlayer )
{
	if ( ( pPlayer->index == render->GetViewEntity() ) && !C_BasePlayer::ShouldDrawLocalPlayer() )
		return false;

	return true;
}


//-----------------------------------------------------------------------------
// Purpose: The mode can choose to not draw fog
//-----------------------------------------------------------------------------
bool ClientModeShared::ShouldDrawFog( void )
{
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void ClientModeShared::AdjustEngineViewport( int& x, int& y, int& width, int& height )
{
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void ClientModeShared::PreRender( CViewSetup *pSetup )
{
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void ClientModeShared::PostRender()
{
	// Let the particle manager simulate things that haven't been simulated.
	ParticleMgr()->PostRender();
}

void ClientModeShared::PostRenderVGui()
{
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void ClientModeShared::Update()
{
#if defined( REPLAY_ENABLED )
	UpdateReplayMessages();
#endif

	if ( m_pViewport->IsVisible() != cl_drawhud.GetBool() )
	{
		m_pViewport->SetVisible( cl_drawhud.GetBool() );
	}

	UpdateRumbleEffects();

	if ( cl_show_num_particle_systems.GetBool() )
	{
		int nCount = 0;

		for ( int i = 0; i < g_pParticleSystemMgr->GetParticleSystemCount(); i++ )
		{
			const char *pParticleSystemName = g_pParticleSystemMgr->GetParticleSystemNameFromIndex(i);
			CParticleSystemDefinition *pParticleSystem = g_pParticleSystemMgr->FindParticleSystem( pParticleSystemName );
			if ( !pParticleSystem )
				continue;

			for ( CParticleCollection *pCurCollection = pParticleSystem->FirstCollection();
				  pCurCollection != NULL;
				  pCurCollection = pCurCollection->GetNextCollectionUsingSameDef() )
			{
				++nCount;
			}
		}

		engine->Con_NPrintf( 0, "# Active particle systems: %i", nCount );
	}
}

//-----------------------------------------------------------------------------
// This processes all input before SV Move messages are sent
//-----------------------------------------------------------------------------

void ClientModeShared::ProcessInput(bool bActive)
{
	gHUD.ProcessInput( bActive );
}

//-----------------------------------------------------------------------------
// Purpose: We've received a keypress from the engine. Return 1 if the engine is allowed to handle it.
//-----------------------------------------------------------------------------
int	ClientModeShared::KeyInput( int down, ButtonCode_t keynum, const char *pszCurrentBinding )
{
	if ( engine->Con_IsVisible() )
		return 1;
	
	// Should we start typing a message?
	if ( pszCurrentBinding &&
		( Q_strcmp( pszCurrentBinding, "messagemode" ) == 0 ||
		  Q_strcmp( pszCurrentBinding, "say" ) == 0 ) )
	{
		if ( down )
		{
			StartMessageMode( MM_SAY );
		}
		return 0;
	}
	else if ( pszCurrentBinding &&
				( Q_strcmp( pszCurrentBinding, "messagemode2" ) == 0 ||
				  Q_strcmp( pszCurrentBinding, "say_team" ) == 0 ) )
	{
		if ( down )
		{
			StartMessageMode( MM_SAY_TEAM );
		}
		return 0;
	}
	else if ( pszCurrentBinding &&
		( Q_strcmp( pszCurrentBinding, "messagemode3" ) == 0 ||
			  Q_strcmp( pszCurrentBinding, "say_party" ) == 0 ) )
	{
		if ( down && BCanSendPartyChatMessages() )
		{
			StartMessageMode( MM_SAY_PARTY );
		}
		return 0;
	}
	
	// If we're voting...
#ifdef VOTING_ENABLED
	CHudVote *pHudVote = GET_HUDELEMENT( CHudVote );
	if ( pHudVote && pHudVote->IsVisible() )
	{
		if ( !pHudVote->KeyInput( down, keynum, pszCurrentBinding ) )
		{
			return 0;
		}
	}
#endif

	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();

	// if ingame spectator mode, let spectator input intercept key event here
	if( pPlayer &&
		( pPlayer->GetObserverMode() > OBS_MODE_DEATHCAM ) &&
		!HandleSpectatorKeyInput( down, keynum, pszCurrentBinding ) )
	{
		return 0;
	}

	// Let game-specific hud elements get a crack at the key input
	if ( !HudElementKeyInput( down, keynum, pszCurrentBinding ) )
	{
		return 0;
	}

	C_BaseCombatWeapon *pWeapon = GetActiveWeapon();
	if ( pWeapon )
	{
		return pWeapon->KeyInput( down, keynum, pszCurrentBinding );
	}

	return 1;
}

#ifdef NEO
extern ConVar glow_outline_effect_enable;
#endif // NEO
//-----------------------------------------------------------------------------
// Purpose: See if spectator input occurred. Return 0 if the key is swallowed.
//-----------------------------------------------------------------------------
int ClientModeShared::HandleSpectatorKeyInput( int down, ButtonCode_t keynum, const char *pszCurrentBinding )
{
	// we are in spectator mode, open spectator menu
	if ( down && pszCurrentBinding && Q_strcmp( pszCurrentBinding, "+duck" ) == 0 )
	{
#ifdef NEO
		// This feature seems pretty broken, you can get stuck in menus etc.
		// Disabling entirely for now.
#if(0)
		m_pViewport->ShowPanel( PANEL_SPECMENU, true );
#endif
#endif
		return 0; // we handled it, don't handle twice or send to server
	}
#ifdef NEO
	else if (down && pszCurrentBinding &&
			 (Q_strcmp(pszCurrentBinding, "+specnextplayer") == 0 || Q_strcmp(pszCurrentBinding, "+attack") == 0))
#else
	else if ( down && pszCurrentBinding && Q_strcmp( pszCurrentBinding, "+attack" ) == 0 )
#endif
	{
		engine->ClientCmd( "spec_next" );
		return 0;
	}
#ifdef NEO
	else if (down && pszCurrentBinding &&
			 (Q_strcmp(pszCurrentBinding, "+specprevplayer") == 0 || Q_strcmp(pszCurrentBinding, "+aim") == 0))
#else
	else if ( down && pszCurrentBinding && Q_strcmp( pszCurrentBinding, "+attack2" ) == 0 )
#endif
	{
		engine->ClientCmd( "spec_prev" );
		return 0;
	}
	else if ( down && pszCurrentBinding && Q_strcmp( pszCurrentBinding, "+jump" ) == 0 )
	{
		engine->ClientCmd( "spec_mode" );
		return 0;
	}
	else if ( down && pszCurrentBinding && Q_strcmp( pszCurrentBinding, "+strafe" ) == 0 )
	{
		HLTVCamera()->SetAutoDirector( true );
#if defined( REPLAY_ENABLED )
		ReplayCamera()->SetAutoDirector( true );
#endif
		return 0;
	}
#ifdef GLOWS_ENABLE
	else if (down && pszCurrentBinding && Q_strcmp(pszCurrentBinding, "+attack2") == 0)
	{
		glow_outline_effect_enable.SetValue(!glow_outline_effect_enable.GetBool());
		return 0;
	}
#endif // GLOWS_ENABLE

	return 1;
}

//-----------------------------------------------------------------------------
// Purpose: See if hud elements want key input. Return 0 if the key is swallowed
//-----------------------------------------------------------------------------
int ClientModeShared::HudElementKeyInput( int down, ButtonCode_t keynum, const char *pszCurrentBinding )
{
	if ( m_pWeaponSelection )
	{
		if ( !m_pWeaponSelection->KeyInput( down, keynum, pszCurrentBinding ) )
		{
			return 0;
		}
	}

#if defined( REPLAY_ENABLED )
	if ( m_pReplayReminderPanel )
	{
		if ( m_pReplayReminderPanel->HudElementKeyInput( down, keynum, pszCurrentBinding ) )
		{
			return 0;
		}
	}
#endif

	return 1;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool ClientModeShared::DoPostScreenSpaceEffects( const CViewSetup *pSetup )
{
#ifdef GLOWS_ENABLE
	g_GlowObjectManager.RenderGlowEffects(pSetup, 0);
#endif
#if defined( REPLAY_ENABLED )
	if ( engine->IsPlayingDemo() )
	{
		if ( !replay_rendersetting_renderglow.GetBool() )
			return false;
	}
#endif 
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : vgui::Panel
//-----------------------------------------------------------------------------
vgui::Panel *ClientModeShared::GetMessagePanel()
{
	if ( m_pChatElement && m_pChatElement->GetInputPanel() && m_pChatElement->GetInputPanel()->IsVisible() )
		return m_pChatElement->GetInputPanel();

	return NULL;
}

//-----------------------------------------------------------------------------
// Purpose: The player has started to type a message
//-----------------------------------------------------------------------------
void ClientModeShared::StartMessageMode( int iMessageModeType )
{
	// Can only show chat UI in multiplayer!!!
	if ( gpGlobals->maxClients == 1 )
	{
		return;
	}
	
#if defined( TF_CLIENT_DLL )
	bool bSuspensionInMatch = GTFGCClientSystem() && GTFGCClientSystem()->BHaveChatSuspensionInCurrentMatch();
	if ( !cl_enable_text_chat.GetBool() || bSuspensionInMatch )
	{
		CBaseHudChat *pHUDChat = ( CBaseHudChat * ) GET_HUDELEMENT( CHudChat );
		if ( pHUDChat )
		{
			const char *pszReason = "#TF_Chat_Disabled";
			if ( bSuspensionInMatch )
			{
				pszReason = "#TF_Chat_Unavailable";
			}

			char szLocalized[100];
			g_pVGuiLocalize->ConvertUnicodeToANSI( g_pVGuiLocalize->Find( pszReason ), szLocalized, sizeof( szLocalized ) );
			pHUDChat->ChatPrintf( 0, CHAT_FILTER_NONE, "%s ", szLocalized );
		}
		return;
	}
#endif // TF_CLIENT_DLL

	if ( m_pChatElement )
	{
		m_pChatElement->StartMessageMode( iMessageModeType );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *newmap - 
//-----------------------------------------------------------------------------
void ClientModeShared::LevelInit( const char *newmap )
{
	m_pViewport->GetAnimationController()->StartAnimationSequence("LevelInit");

	// Tell the Chat Interface
	if ( m_pChatElement )
	{
		m_pChatElement->LevelInit( newmap );
	}

	// we have to fake this event clientside, because clients connect after that
	IGameEvent *event = gameeventmanager->CreateEvent( "game_newmap" );
	if ( event )
	{
		event->SetString("mapname", newmap );
		gameeventmanager->FireEventClientSide( event );
	}

	// Create a vgui context for all of the in-game vgui panels...
	if ( s_hVGuiContext == DEFAULT_VGUI_CONTEXT )
	{
		s_hVGuiContext = vgui::ivgui()->CreateContext();
	}

	// Reset any player explosion/shock effects
	CLocalPlayerFilter filter;
	enginesound->SetPlayerDSP( filter, 0, true );

#ifdef NEO
	if (g_pNeoLoading)
	{
		g_pVGuiLocalize->ConvertANSIToUnicode(newmap, g_pNeoLoading->m_wszLoadingMap, sizeof(g_pNeoLoading->m_wszLoadingMap));
	}
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void ClientModeShared::LevelShutdown( void )
{
	// Reset the third person camera so we don't crash
	g_ThirdPersonManager.Init();

	if ( m_pChatElement )
	{
		m_pChatElement->LevelShutdown();
	}
	if ( s_hVGuiContext != DEFAULT_VGUI_CONTEXT )
	{
		vgui::ivgui()->DestroyContext( s_hVGuiContext );
 		s_hVGuiContext = DEFAULT_VGUI_CONTEXT;
	}

	// Reset any player explosion/shock effects
	CLocalPlayerFilter filter;
	enginesound->SetPlayerDSP( filter, 0, true );

#ifdef NEO
	if (g_pNeoLoading)
	{
		g_pNeoLoading->m_wszLoadingMap[0] = L'\0';
	}
#endif
}


void ClientModeShared::Enable()
{
	vgui::VPANEL pRoot = VGui_GetClientDLLRootPanel();

	// Add our viewport to the root panel.
	if( pRoot != 0 )
	{
		m_pViewport->SetParent( pRoot );
	}

	// All hud elements should be proportional
	// This sets that flag on the viewport and all child panels
	m_pViewport->SetProportional( true );

	m_pViewport->SetCursor( m_CursorNone );
	vgui::surface()->SetCursor( m_CursorNone );

	m_pViewport->SetVisible( true );
	if ( m_pViewport->IsKeyBoardInputEnabled() )
	{
		m_pViewport->RequestFocus();
	}

	Layout();
}


void ClientModeShared::Disable()
{
	vgui::VPANEL pRoot = VGui_GetClientDLLRootPanel();

	// Remove our viewport from the root panel.
	if( pRoot != 0 )
	{
		m_pViewport->SetParent( (vgui::VPANEL)NULL );
	}

	m_pViewport->SetVisible( false );
}


void ClientModeShared::Layout()
{
	vgui::VPANEL pRoot = VGui_GetClientDLLRootPanel();
	int wide, tall;

	// Make the viewport fill the root panel.
	if( pRoot != 0 )
	{
		vgui::ipanel()->GetSize(pRoot, wide, tall);

		bool changed = wide != m_nRootSize[ 0 ] || tall != m_nRootSize[ 1 ];
		m_nRootSize[ 0 ] = wide;
		m_nRootSize[ 1 ] = tall;

		m_pViewport->SetBounds(0, 0, wide, tall);
		if ( changed )
		{
			ReloadScheme(false);
		}
	}
}

float ClientModeShared::GetViewModelFOV( void )
{
	return v_viewmodel_fov.GetFloat();
}

class CHudChat;

bool PlayerNameNotSetYet( const char *pszName )
{
	if ( !pszName || !pszName[0] )
		return true;

	// Don't show "unconnected" if we haven't got the players name yet
	if ( Q_strnicmp(pszName,"unconnected",11) == 0 )
		return true;
	if ( Q_strnicmp(pszName,"NULLNAME",11) == 0 )
		return true;

	return false;
}

void ClientModeShared::FireGameEvent( IGameEvent *event )
{
	CBaseHudChat *hudChat = (CBaseHudChat *)GET_HUDELEMENT( CHudChat );

	const char *eventname = event->GetName();

	if ( Q_strcmp( "player_connect_client", eventname ) == 0 )
	{
		if ( !hudChat )
			return;
		if ( PlayerNameNotSetYet(event->GetString("name")) )
			return;

		if ( !IsInCommentaryMode() )
		{
#ifdef NEO
			if (cl_neo_streamermode.GetBool())
			{
				hudChat->Printf(CHAT_FILTER_JOINLEAVE, "Player connected");
				return;
			}
#endif
			wchar_t wszLocalized[100];
			wchar_t wszPlayerName[ MAX_PLAYER_NAME_LENGTH ];
			int iPlayerIndex = engine->GetPlayerForUserID( event->GetInt( "userid" ) );
			UTIL_GetFilteredPlayerNameAsWChar( iPlayerIndex, event->GetString( "name" ), wszPlayerName );
			g_pVGuiLocalize->ConstructString_safe( wszLocalized, g_pVGuiLocalize->Find( "#game_player_joined_game" ), 1, wszPlayerName );

			char szLocalized[100];
			g_pVGuiLocalize->ConvertUnicodeToANSI( wszLocalized, szLocalized, sizeof(szLocalized) );

			hudChat->Printf( CHAT_FILTER_JOINLEAVE, "%s", szLocalized );
		}
	}
	else if ( Q_strcmp( "player_disconnect", eventname ) == 0 )
	{
#ifdef NEO
		if (cl_neo_streamermode.GetBool())
		{
			hudChat->Printf(CHAT_FILTER_JOINLEAVE, "Player disconnected");
			return;
		}
#endif

#ifndef NEO // ???
		C_BasePlayer *pPlayer = USERID2PLAYER( event->GetInt("userid") );
		if ( !hudChat || !pPlayer )
#endif
		// Josh: There used to be code here that would get the player entity here to get the name
		// Big problem with that. The player entity is probably already gone -- they disconnected after all!
		// So there used to be a bug where most of the time, disconnect messages just wouldn't show up in chat.
		//
		// The player's name who disconnected is already provided in the event, so there was no reason for all
		// of this logic anyway...
		if ( !hudChat )
			return;

		const char* pszPlayerName = event->GetString( "name" );

		if ( PlayerNameNotSetYet( pszPlayerName ) )
			return;

		if ( !IsInCommentaryMode() )
		{
			wchar_t wszPlayerName[MAX_PLAYER_NAME_LENGTH];
			g_pVGuiLocalize->ConvertANSIToUnicode( pszPlayerName, wszPlayerName, sizeof(wszPlayerName) );

			wchar_t wszReason[64];
			const char *pszReason = event->GetString( "reason" );
			if ( pszReason && ( pszReason[0] == '#' ) && g_pVGuiLocalize->Find( pszReason ) )
			{
				V_wcsncpy( wszReason, g_pVGuiLocalize->Find( pszReason ), sizeof( wszReason ) );
			}
			else
			{
				g_pVGuiLocalize->ConvertANSIToUnicode( pszReason, wszReason, sizeof(wszReason) );
			}

			wchar_t wszLocalized[100];
			if (IsPC())
			{
				g_pVGuiLocalize->ConstructString_safe( wszLocalized, g_pVGuiLocalize->Find( "#game_player_left_game" ), 2, wszPlayerName, wszReason );
			}
			else
			{
				g_pVGuiLocalize->ConstructString_safe( wszLocalized, g_pVGuiLocalize->Find( "#game_player_left_game" ), 1, wszPlayerName );
			}

			char szLocalized[100];
			g_pVGuiLocalize->ConvertUnicodeToANSI( wszLocalized, szLocalized, sizeof(szLocalized) );

			hudChat->Printf( CHAT_FILTER_JOINLEAVE, "%s", szLocalized );
		}
	}
	else if ( Q_strcmp( "player_team", eventname ) == 0 )
	{
		C_BasePlayer *pPlayer = USERID2PLAYER( event->GetInt("userid") );
#ifdef NEO
#ifdef GLOWS_ENABLE
		if (pPlayer && glow_outline_effect_enable.GetBool())
		{ // NEO JANK (Adam) bots join their final team before they are created client side, so pPlayer here will be null for them, and setting clientsideglow on them in c_neo_player::Spawn() results in an incorrect glow colour. This works for players though
			float r, g, b;
			NEORules()->GetTeamGlowColor(event->GetInt("team"), r, g, b);
			pPlayer->SetGlowEffectColor(r, g, b);
			pPlayer->SetClientSideGlowEnabled(true);
		}
#endif // GLOWS_ENABLE
#endif // NEO

		if ( !hudChat )
			return;

		bool bDisconnected = event->GetBool("disconnect");

		if ( bDisconnected )
			return;

		int team = event->GetInt( "team" );
		bool bAutoTeamed = event->GetInt( "autoteam", false );
		bool bSilent = event->GetInt( "silent", false );

		const char *pszName = event->GetString( "name" );
		if ( PlayerNameNotSetYet( pszName ) )
			return;

		if ( !bSilent )
		{
			wchar_t wszPlayerName[MAX_PLAYER_NAME_LENGTH];
			int iPlayerIndex = engine->GetPlayerForUserID( event->GetInt( "userid" ) );
			UTIL_GetFilteredPlayerNameAsWChar( iPlayerIndex, pszName, wszPlayerName );

			bool bUsingCustomTeamName = false;
#ifdef TF_CLIENT_DLL
			C_TFTeam *pTeam = GetGlobalTFTeam( team );
			const wchar_t *wszTeam = pTeam ? pTeam->Get_Localized_Name() : L"";
			bUsingCustomTeamName = pTeam ? pTeam->IsUsingCustomTeamName() : false;
#else
			wchar_t wszTeam[64];
			C_Team *pTeam = GetGlobalTeam( team );
			if ( pTeam )
			{
				g_pVGuiLocalize->ConvertANSIToUnicode( pTeam->Get_Name(), wszTeam, sizeof(wszTeam) );
			}
			else
			{
				_snwprintf ( wszTeam, sizeof( wszTeam ) / sizeof( wchar_t ), L"%d", team );
			}
#endif

			if ( !IsInCommentaryMode() )
			{
				bool weWantToChat = false;

				wchar_t wszLocalized[100];
				if ( bAutoTeamed )
				{
#if 0 // TODO (nullsystem): 2025-02-18 SOURCE SDK 2013 CHECK
					g_pVGuiLocalize->ConstructString( wszLocalized, sizeof( wszLocalized ), g_pVGuiLocalize->Find( "#game_player_joined_autoteam" ), 2, wszPlayerName, wszTeam );
					weWantToChat = true;
				}
				else
				{
					int oldteam = event->GetInt("oldteam");

					// NEO NOTE (Rain):
					// Avoid a "client joined spectator" on initial player connect,
					// as they will most likely choose a team to join from the HUD,
					// resulting in yet another "client joined Jinrai" message or similar.
					// An unassigned->spectator is always a new client, because clients
					// cannot join team unassigned manually.
					if (oldteam != TEAM_UNASSIGNED && team != TEAM_SPECTATOR)
					{
						g_pVGuiLocalize->ConstructString(wszLocalized, sizeof(wszLocalized), g_pVGuiLocalize->Find("#game_player_joined_team"), 2, wszPlayerName, wszTeam);
					}
#endif
					g_pVGuiLocalize->ConstructString_safe( wszLocalized, bUsingCustomTeamName ? g_pVGuiLocalize->Find( "#game_player_joined_autoteam_party_leader" ) : g_pVGuiLocalize->Find( "#game_player_joined_autoteam" ), 2, wszPlayerName, wszTeam );
				}
				else
				{
					g_pVGuiLocalize->ConstructString_safe( wszLocalized, bUsingCustomTeamName ? g_pVGuiLocalize->Find( "#game_player_joined_team_party_leader" ) : g_pVGuiLocalize->Find( "#game_player_joined_team" ), 2, wszPlayerName, wszTeam );
				}

				if (weWantToChat)
				{
					char szLocalized[100];
					g_pVGuiLocalize->ConvertUnicodeToANSI(wszLocalized, szLocalized, sizeof(szLocalized));

					hudChat->Printf(CHAT_FILTER_TEAMCHANGE, "%s", szLocalized);
				}
			}
		}

		if ( pPlayer && pPlayer->IsLocalPlayer() )
		{
			// that's me
			pPlayer->TeamChange( team );
		}
	}
#ifdef NEO
	else if (Q_strcmp("player_changename", eventname) == 0 || Q_strcmp("player_changeneoname", eventname) == 0)
#else
	else if ( Q_strcmp( "player_changename", eventname ) == 0 )
#endif
	{
		if ( !hudChat )
			return;

		const char *pszOldName = event->GetString("oldname");
		if ( PlayerNameNotSetYet(pszOldName) )
			return;

#ifdef NEO
		const bool thisClientShowChange = C_NEO_Player::GetLocalNEOPlayer()->ClientWantNeoName();
		if (Q_strcmp("player_changeneoname", eventname) == 0 && !thisClientShowChange)
		{
			// The client dont want to see neo_name changes so return early.
			return;
		}
#endif

		int iPlayerIndex = engine->GetPlayerForUserID( event->GetInt( "userid" ) );

		wchar_t wszOldName[ MAX_PLAYER_NAME_LENGTH ];
		UTIL_GetFilteredPlayerNameAsWChar( iPlayerIndex, pszOldName, wszOldName );

		wchar_t wszNewName[ MAX_PLAYER_NAME_LENGTH ];
		UTIL_GetFilteredPlayerNameAsWChar( iPlayerIndex, event->GetString( "newname" ), wszNewName );

		wchar_t wszLocalized[100];
		g_pVGuiLocalize->ConstructString_safe( wszLocalized, g_pVGuiLocalize->Find( "#game_player_changed_name" ), 2, wszOldName, wszNewName );

		char szLocalized[100];
		g_pVGuiLocalize->ConvertUnicodeToANSI( wszLocalized, szLocalized, sizeof(szLocalized) );

		hudChat->Printf( CHAT_FILTER_NAMECHANGE, "%s", szLocalized );
	}
	else if ( Q_strcmp( "teamplay_broadcast_audio", eventname ) == 0 )
	{
		int team = event->GetInt( "team" );

		bool bValidTeam = false;

		if ( GetLocalTeam() && ( GetLocalTeam()->GetTeamNumber() == team ) )
		{
			bValidTeam = true;
		}

		//If we're in the spectator team then we should be getting whatever messages the person I'm spectating gets.
		if ( bValidTeam == false )
		{
			CBasePlayer *pSpectatorTarget = UTIL_PlayerByIndex( GetSpectatorTarget() );

			if ( pSpectatorTarget && ( GetSpectatorMode() == OBS_MODE_IN_EYE || GetSpectatorMode() == OBS_MODE_CHASE || GetSpectatorMode() == OBS_MODE_POI ) )
			{
				if ( pSpectatorTarget->GetTeamNumber() == team )
				{
					bValidTeam = true;
				}
			}
		}

		if ( team == 0 && GetLocalTeam() != nullptr )
		{
			bValidTeam = false;
		}

		if ( team == 255 )
		{
			bValidTeam = true;
		}

		if ( bValidTeam == true )
		{
			EmitSound_t et;
			et.m_pSoundName = event->GetString( "sound" );
			et.m_nFlags = event->GetInt( "additional_flags" );

#ifdef TF_CLIENT_DLL
			int iPlayerIndex = event->GetInt( "player" );
			if ( iPlayerIndex > 0 )
			{
				CTFPlayer *pTFPlayer = ToTFPlayer( UTIL_PlayerByIndex( iPlayerIndex ) );
				if ( pTFPlayer )
				{
					pTFPlayer->ClientAdjustStartSoundParams( et );
				}
			}
#endif // TF_CLIENT_DLL

			CLocalPlayerFilter filter;
			C_BaseEntity::EmitSound( filter, SOUND_FROM_LOCAL_PLAYER, et );
		}
	}
	else if ( Q_strcmp( "server_cvar", eventname ) == 0 )
	{
		if ( !IsInCommentaryMode() )
		{
			wchar_t wszCvarName[64];
			g_pVGuiLocalize->ConvertANSIToUnicode( event->GetString("cvarname"), wszCvarName, sizeof(wszCvarName) );

			wchar_t wszCvarValue[64];
			g_pVGuiLocalize->ConvertANSIToUnicode( event->GetString("cvarvalue"), wszCvarValue, sizeof(wszCvarValue) );

			wchar_t wszLocalized[256];
			g_pVGuiLocalize->ConstructString_safe( wszLocalized, g_pVGuiLocalize->Find( "#game_server_cvar_changed" ), 2, wszCvarName, wszCvarValue );

			char szLocalized[256];
			g_pVGuiLocalize->ConvertUnicodeToANSI( wszLocalized, szLocalized, sizeof(szLocalized) );

			hudChat->Printf( CHAT_FILTER_SERVERMSG, "%s", szLocalized );
		}
	}
	else if ( Q_strcmp( "achievement_earned", eventname ) == 0 )
	{
		int iPlayerIndex = event->GetInt( "player" );
		C_BasePlayer *pPlayer = UTIL_PlayerByIndex( iPlayerIndex );
		int iAchievement = event->GetInt( "achievement" );

		if ( !hudChat || !pPlayer )
			return;

		if ( !IsInCommentaryMode() )
		{
			CAchievementMgr *pAchievementMgr = dynamic_cast<CAchievementMgr *>( engine->GetAchievementMgr() );
			if ( !pAchievementMgr )
				return;

			IAchievement *pAchievement = pAchievementMgr->GetAchievementByID( iAchievement );
			if ( pAchievement )
			{
				if ( !pPlayer->IsDormant() && pPlayer->ShouldAnnounceAchievement() )
				{
					pPlayer->SetNextAchievementAnnounceTime( gpGlobals->curtime + ACHIEVEMENT_ANNOUNCEMENT_MIN_TIME );

#ifndef NEO
					// no particle effect if the local player is the one with the achievement or the player is dead
					if ( !pPlayer->IsLocalPlayer() && pPlayer->IsAlive() ) 
					{
						pPlayer->ParticleProp()->Create( "achieved", PATTACH_POINT_FOLLOW, "head" );
					}
#endif // NEO

					pPlayer->OnAchievementAchieved( iAchievement );
				}

				if ( g_PR )
				{
					wchar_t wszPlayerName[MAX_PLAYER_NAME_LENGTH];
					g_pVGuiLocalize->ConvertANSIToUnicode( g_PR->GetPlayerName( iPlayerIndex ), wszPlayerName, sizeof( wszPlayerName ) );

					const wchar_t *pchLocalizedAchievement = ACHIEVEMENT_LOCALIZED_NAME_FROM_STR( pAchievement->GetName() );
					if ( pchLocalizedAchievement )
					{
						wchar_t wszLocalizedString[128];
						g_pVGuiLocalize->ConstructString_safe( wszLocalizedString, g_pVGuiLocalize->Find( "#Achievement_Earned" ), 2, wszPlayerName, pchLocalizedAchievement );

						char szLocalized[128];
						g_pVGuiLocalize->ConvertUnicodeToANSI( wszLocalizedString, szLocalized, sizeof( szLocalized ) );

						hudChat->ChatPrintf( iPlayerIndex, CHAT_FILTER_SERVERMSG, "%s", szLocalized );
					}
				}
			}
		}
	}
#if defined( TF_CLIENT_DLL )
	else if ( Q_strcmp( "item_found", eventname ) == 0 )
	{
		int iPlayerIndex = event->GetInt( "player" );
		entityquality_t iItemQuality = event->GetInt( "quality" );
		int iMethod = event->GetInt( "method" );
		int iItemDef = event->GetInt( "itemdef" );
		bool bIsStrange = event->GetInt( "isstrange" );
		bool bIsUnusual = event->GetInt( "isunusual" );
		float flWear = event->GetFloat( "wear" );

		C_BasePlayer *pPlayer = UTIL_PlayerByIndex( iPlayerIndex );
		const GameItemDefinition_t *pItemDefinition = dynamic_cast<GameItemDefinition_t *>( GetItemSchema()->GetItemDefinition( iItemDef ) );

		if ( !pPlayer || !pItemDefinition || pItemDefinition->IsHidden() )
			return;

		if ( g_PR )
		{
			wchar_t wszPlayerName[MAX_PLAYER_NAME_LENGTH];
			g_pVGuiLocalize->ConvertANSIToUnicode( g_PR->GetPlayerName( iPlayerIndex ), wszPlayerName, sizeof( wszPlayerName ) );

			if ( iMethod < 0 || iMethod >= ARRAYSIZE( g_pszItemFoundMethodStrings ) )
			{
				iMethod = 0;
			}

			const char *pszLocString = g_pszItemFoundMethodStrings[iMethod];
			if ( pszLocString )
			{
				wchar_t wszItemFound[256];
				_snwprintf( wszItemFound, ARRAYSIZE( wszItemFound ), L"%ls", g_pVGuiLocalize->Find( pszLocString ) );

				wchar_t *colorMarker = wcsstr( wszItemFound, L"::" );
				const CEconItemRarityDefinition* pItemRarity = GetItemSchema()->GetRarityDefinition( pItemDefinition->GetRarity() );

				if ( colorMarker )
				{	
					if ( pItemRarity )
					{
						attrib_colors_t colorRarity = pItemRarity->GetAttribColor();
						vgui::HScheme scheme = vgui::scheme()->GetScheme( "ClientScheme" );
						vgui::IScheme *pScheme = vgui::scheme()->GetIScheme( scheme );
						Color color = pScheme->GetColor( GetColorNameForAttribColor( colorRarity ), Color( 255, 255, 255, 255 ) );
						hudChat->SetCustomColor( color );
					}
					else
					{
						const char *pszQualityColorString = EconQuality_GetColorString( (EEconItemQuality)iItemQuality );
						if ( pszQualityColorString )
						{
							hudChat->SetCustomColor( pszQualityColorString );
						}
					}

					*(colorMarker+1) = COLOR_CUSTOM;
				}

				// TODO: Update the localization strings to only have two format parameters since that's all we need.
				locchar_t wszLocalizedString[256];

				locchar_t szItemname[64] = LOCCHAR( "" );
				locchar_t szRarity[64] = LOCCHAR( "" );
				locchar_t szWear[64] = LOCCHAR( "" );
				locchar_t szStrange[64] = LOCCHAR( "" );
				locchar_t szUnusual[64] = LOCCHAR( "" );

				loc_scpy_safe(
					szItemname, 
					CConstructLocalizedString(g_pVGuiLocalize->Find("TFUI_InvTooltip_ItemFound_Itemname"), 
					CEconItemLocalizedFullNameGenerator(GLocalizationProvider(), pItemDefinition, iItemQuality).GetFullName() )
				);

				/*g_pVGuiLocalize->ConstructString_safe( 
					szItemname, 
					LOCCHAR( "%s1 " ),
					1, 
					CEconItemLocalizedFullNameGenerator( GLocalizationProvider(), pItemDefinition, iItemQuality ).GetFullName()
				);*/

				locchar_t tempName[MAX_ITEM_NAME_LENGTH];
				// If items have rarity
				if ( pItemRarity )
				{
					// Weapon Wear
					if ( !IsWearableSlot( pItemDefinition->GetDefaultLoadoutSlot() ) )
					{
						loc_scpy_safe(szWear, CConstructLocalizedString( g_pVGuiLocalize->Find("TFUI_InvTooltip_ItemFound_Wear"), g_pVGuiLocalize->Find(GetWearLocalizationString(flWear) ) ) );
					}

					// Rarity / grade
					loc_scpy_safe(szRarity, CConstructLocalizedString(g_pVGuiLocalize->Find("TFUI_InvTooltip_ItemFound_Rarity"), g_pVGuiLocalize->Find(pItemRarity->GetLocKey() ) ) );
				}

				if ( bIsUnusual )
				{
					loc_scpy_safe(szUnusual, CConstructLocalizedString(g_pVGuiLocalize->Find("TFUI_InvTooltip_ItemFound_Unusual"), g_pVGuiLocalize->Find("rarity4")));
				}

				if ( bIsStrange )
				{
					loc_scpy_safe(szStrange, CConstructLocalizedString(g_pVGuiLocalize->Find("TFUI_InvTooltip_ItemFound_Strange"), g_pVGuiLocalize->Find("strange")));
				}

				// // Strange Unusual Item Grade 		
				loc_scpy_safe( wszLocalizedString, CConstructLocalizedString( g_pVGuiLocalize->Find( "TFUI_InvTooltip_ItemFound" ), szStrange, szUnusual, szItemname, szRarity, szWear ) );

				loc_scpy_safe( tempName, wszLocalizedString );
				g_pVGuiLocalize->ConstructString_safe(
					wszLocalizedString,
					wszItemFound,
					3,
					wszPlayerName, tempName, L"" );

				char szLocalized[256];
				g_pVGuiLocalize->ConvertUnicodeToANSI( wszLocalizedString, szLocalized, sizeof( szLocalized ) );

				hudChat->ChatPrintf( iPlayerIndex, CHAT_FILTER_SERVERMSG, "%s", szLocalized );
			}
		}		
	}
#endif
#if defined( REPLAY_ENABLED )
	else if ( !V_strcmp( "replay_servererror", eventname ) )
	{
		DisplayReplayMessage( event->GetString( "error", "#Replay_DefaultServerError" ), replay_msgduration_error.GetFloat(), true, NULL, false );
	}
	else if ( !V_strcmp( "replay_startrecord", eventname ) )
	{
		m_flReplayStartRecordTime = gpGlobals->curtime;
	}
	else if ( !V_strcmp( "replay_endrecord", eventname ) )
	{
		m_flReplayStopRecordTime = gpGlobals->curtime;
	}
	else if ( !V_strcmp( "replay_replaysavailable", eventname ) )
	{
		DisplayReplayMessage( "#Replay_ReplaysAvailable", replay_msgduration_replaysavailable.GetFloat(), false, NULL, false );
	}

	else if ( !V_strcmp( "game_newmap", eventname ) )
	{
		// Make sure the instance count is reset to 0.  Sometimes the count stay in sync and we get replay messages displaying lower than they should.
		CReplayMessagePanel::RemoveAll();
	}
#endif

	else
	{
		DevMsg( 2, "Unhandled GameEvent in ClientModeShared::FireGameEvent - %s\n", event->GetName()  );
	}
}

void ClientModeShared::UpdateReplayMessages()
{
#if defined( REPLAY_ENABLED )
	// Received a replay_startrecord event?
	if ( m_flReplayStartRecordTime != 0.0f )
	{
		DisplayReplayMessage( "#Replay_StartRecord", replay_msgduration_startrecord.GetFloat(), true, "replay\\startrecord.mp3", false );

		m_flReplayStartRecordTime = 0.0f;
		m_flReplayStopRecordTime = 0.0f;
	}

	// Received a replay_endrecord event?
	if ( m_flReplayStopRecordTime != 0.0f )
	{
		DisplayReplayMessage( "#Replay_EndRecord", replay_msgduration_stoprecord.GetFloat(), true, "replay\\stoprecord.wav", false );

		// Hide the replay reminder
		if ( m_pReplayReminderPanel )
		{
			m_pReplayReminderPanel->Hide();
		}

		m_flReplayStopRecordTime = 0.0f;
	}

	if ( !engine->IsConnected() )
	{
		ClearReplayMessageList();
	}
#endif
}

void ClientModeShared::ClearReplayMessageList()
{
#if defined( REPLAY_ENABLED )
	CReplayMessagePanel::RemoveAll();
#endif
}

void ClientModeShared::DisplayReplayMessage( const char *pLocalizeName, float flDuration, bool bUrgent,
											 const char *pSound, bool bDlg )
{
#if defined( REPLAY_ENABLED )
	// Don't display during replay playback, and don't allow more than 4 at a time
	const bool bInReplay = g_pEngineClientReplay->IsPlayingReplayDemo();
	if ( bInReplay || ( !bDlg && CReplayMessagePanel::InstanceCount() >= 4 ) )
		return;

	// Use default duration?
	if ( flDuration == -1.0f )
	{
		flDuration = replay_msgduration_misc.GetFloat();
	}

	// Display a replay message
	if ( bDlg )
	{
		if ( engine->IsInGame() )
		{
			Panel *pPanel = new CReplayMessageDlg( pLocalizeName );
			pPanel->SetVisible( true );
			pPanel->MakePopup();
			pPanel->MoveToFront();
			pPanel->SetKeyBoardInputEnabled( true );
			pPanel->SetMouseInputEnabled( true );
#if defined( TF_CLIENT_DLL )
			TFModalStack()->PushModal( pPanel );
#endif
		}
		else
		{
			ShowMessageBox( "#Replay_GenericMsgTitle", pLocalizeName, "#GameUI_OK" );
		}
	}
	else
	{
		CReplayMessagePanel *pMsgPanel = new CReplayMessagePanel( pLocalizeName, flDuration, bUrgent );
		pMsgPanel->Show();
	}

	// Play a sound if appropriate
	if ( pSound )
	{
		surface()->PlaySound( pSound );
	}
#endif
}

void ClientModeShared::DisplayReplayReminder()
{
#if defined( REPLAY_ENABLED )
	if ( m_pReplayReminderPanel && g_pReplay->IsRecording() && !::input->IsSteamControllerActive() )
	{
		// Only display the panel if we haven't already requested a replay for the given life
		CReplay *pCurLifeReplay = static_cast< CReplay * >( g_pClientReplayContext->GetReplayManager()->GetReplayForCurrentLife() );
		if ( pCurLifeReplay && !pCurLifeReplay->m_bRequestedByUser && !pCurLifeReplay->m_bSaved )
		{
			m_pReplayReminderPanel->Show();
		}
	}
#endif
}


//-----------------------------------------------------------------------------
// In-game VGUI context 
//-----------------------------------------------------------------------------
void ClientModeShared::ActivateInGameVGuiContext( vgui::Panel *pPanel )
{
	vgui::ivgui()->AssociatePanelWithContext( s_hVGuiContext, pPanel->GetVPanel() );
	vgui::ivgui()->ActivateContext( s_hVGuiContext );
}

void ClientModeShared::DeactivateInGameVGuiContext()
{
	vgui::ivgui()->ActivateContext( DEFAULT_VGUI_CONTEXT );
}



