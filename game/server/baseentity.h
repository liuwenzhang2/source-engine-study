//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef BASEENTITY_H
#define BASEENTITY_H
#ifdef _WIN32
#pragma once
#endif

#define TEAMNUM_NUM_BITS	6

//#include "entitylist.h"
#include "entityoutput.h"
#include "networkvar.h"
#include "collisionproperty.h"
#include "ServerNetworkProperty.h"
#include "shareddefs.h"
#include "engine/ivmodelinfo.h"
#include "SoundEmitterSystem/isoundemittersystembase.h"

class CDamageModifier;
class CDmgAccumulator;

struct CSoundParameters;

class AI_CriteriaSet;
class IResponseSystem;
class IEntitySaveUtils;
class CRecipientFilter;
class IStudioHdr;
template<class T>
class CGlobalEntityList;
class CServerGameDLL;

// Matching the high level concept is significantly better than other criteria
// FIXME:  Could do this in the script file by making it required and bumping up weighting there instead...
#define CONCEPT_WEIGHT 5.0f

typedef CHandle<CBaseEntity> EHANDLE;

//#define MANUALMODE_GETSET_PROP(type, accessorName, varName) \
//	private:\
//		type varName;\
//	public:\
//		inline const type& Get##accessorName##() const { return varName; } \
//		inline type& Get##accessorName##() { return varName; } \
//		inline void Set##accessorName##( const type &val ) { varName = val; m_NetStateMgr.StateChanged(); }
//
//#define MANUALMODE_GETSET_EHANDLE(type, accessorName, varName) \
//	private:\
//		CHandle<type> varName;\
//	public:\
//		inline type* Get##accessorName##() { return varName.Get(); } \
//		inline void Set##accessorName##( type *pType ) { varName = pType; m_NetStateMgr.StateChanged(); }


// saverestore.h declarations
class CSaveRestoreData;
class typedescription_t;
class ISave;
class IRestore;
class CBaseEntity;
class CEntityMapData;
class CBaseCombatWeapon;
class IPhysicsObject;
class IPhysicsShadowController;
class CBaseCombatCharacter;
class CTeam;
class Vector;
struct gamevcollisionevent_t;
class CBaseAnimating;
class CBasePlayer;
class IServerVehicle;
struct solid_t;
struct notify_system_event_params_t;
class CAI_BaseNPC;
class CAI_Senses;
class CSquadNPC;
class variant_t;
class CEventAction;
typedef struct KeyValueData_s KeyValueData;
class CUserCmd;
class CSkyCamera;
class CEntityMapData;
class INextBot;


typedef CUtlVector< CBaseEntity* > EntityList_t;

#if defined( HL2_DLL )

// For CLASSIFY
enum Class_T
{
	CLASS_NONE=0,				
	CLASS_PLAYER,			
	CLASS_PLAYER_ALLY,
	CLASS_PLAYER_ALLY_VITAL,
	CLASS_ANTLION,
	CLASS_BARNACLE,
	CLASS_BULLSEYE,
	//CLASS_BULLSQUID,	
	CLASS_CITIZEN_PASSIVE,	
	CLASS_CITIZEN_REBEL,
	CLASS_COMBINE,
	CLASS_COMBINE_GUNSHIP,
	CLASS_CONSCRIPT,
	CLASS_HEADCRAB,
	//CLASS_HOUNDEYE,
	CLASS_MANHACK,
	CLASS_METROPOLICE,		
	CLASS_MILITARY,		
	CLASS_SCANNER,		
	CLASS_STALKER,		
	CLASS_VORTIGAUNT,
	CLASS_ZOMBIE,
	CLASS_PROTOSNIPER,
	CLASS_MISSILE,
	CLASS_FLARE,
	CLASS_EARTH_FAUNA,
	CLASS_HACKED_ROLLERMINE,
	CLASS_COMBINE_HUNTER,

	NUM_AI_CLASSES
};

#elif defined( HL1_DLL )

enum Class_T
{
	CLASS_NONE = 0,
	CLASS_MACHINE,
	CLASS_PLAYER,
	CLASS_HUMAN_PASSIVE,
	CLASS_HUMAN_MILITARY,
	CLASS_ALIEN_MILITARY,
	CLASS_ALIEN_MONSTER,
	CLASS_ALIEN_PREY,
	CLASS_ALIEN_PREDATOR,
	CLASS_INSECT,
	CLASS_PLAYER_ALLY,
	CLASS_PLAYER_BIOWEAPON,
	CLASS_ALIEN_BIOWEAPON,

	NUM_AI_CLASSES
};

#elif defined( INVASION_DLL )

enum Class_T
{
	CLASS_NONE = 0,
	CLASS_PLAYER,			
	CLASS_PLAYER_ALLY,
	CLASS_PLAYER_ALLY_VITAL,
	CLASS_ANTLION,
	CLASS_BARNACLE,
	CLASS_BULLSEYE,
	//CLASS_BULLSQUID,	
	CLASS_CITIZEN_PASSIVE,	
	CLASS_CITIZEN_REBEL,
	CLASS_COMBINE,
	CLASS_COMBINE_GUNSHIP,
	CLASS_CONSCRIPT,
	CLASS_HEADCRAB,
	//CLASS_HOUNDEYE,
	CLASS_MANHACK,
	CLASS_METROPOLICE,		
	CLASS_MILITARY,		
	CLASS_SCANNER,		
	CLASS_STALKER,		
	CLASS_VORTIGAUNT,
	CLASS_ZOMBIE,
	CLASS_PROTOSNIPER,
	CLASS_MISSILE,
	CLASS_FLARE,
	CLASS_EARTH_FAUNA,
	NUM_AI_CLASSES
};

#elif defined( CSTRIKE_DLL )

enum Class_T
{
	CLASS_NONE = 0,
	CLASS_PLAYER,
	CLASS_PLAYER_ALLY,
	NUM_AI_CLASSES
};

#else

enum Class_T
{
	CLASS_NONE = 0,
	CLASS_PLAYER,
	CLASS_PLAYER_ALLY,
	NUM_AI_CLASSES
};

#endif

//
// Structure passed to input handlers.
//
struct inputdata_t
{
	CBaseEntity *pActivator;		// The entity that initially caused this chain of output events.
	CBaseEntity *pCaller;			// The entity that fired this particular output.
	variant_t value;				// The data parameter for this output.
	int nOutputID;					// The unique ID of the output that was fired.
};

// Serializable list of context as set by entity i/o and used for deducing proper
//  speech state, et al.
struct ResponseContext_t
{
	DECLARE_SIMPLE_DATADESC();

	string_t		m_iszName;
	string_t		m_iszValue;
	float			m_fExpirationTime;		// when to expire context (0 == never)
};


//-----------------------------------------------------------------------------
// Entity events... targetted to a particular entity
// Each event has a well defined structure to use for parameters
//-----------------------------------------------------------------------------
enum EntityEvent_t
{
	ENTITY_EVENT_WATER_TOUCH = 0,		// No data needed
	ENTITY_EVENT_WATER_UNTOUCH,			// No data needed
	ENTITY_EVENT_PARENT_CHANGED,		// No data needed
};

enum notify_system_event_t
{
	NOTIFY_EVENT_TELEPORT = 0,
	NOTIFY_EVENT_DESTROY,
};

//-----------------------------------------------------------------------------

typedef void (CBaseEntity::*ENTITYFUNCPTR)(CBaseEntity *pOther );
typedef void (CBaseEntity::*USEPTR)( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );

#define DEFINE_THINKFUNC( function ) DEFINE_FUNCTION_RAW( function, BASEPTR )
#define DEFINE_ENTITYFUNC( function ) DEFINE_FUNCTION_RAW( function, ENTITYFUNCPTR )
#define DEFINE_USEFUNC( function ) DEFINE_FUNCTION_RAW( function, USEPTR )

// Things that toggle (buttons/triggers/doors) need this
enum TOGGLE_STATE
{
	TS_AT_TOP,
	TS_AT_BOTTOM,
	TS_GOING_UP,
	TS_GOING_DOWN
};


// Debug overlay bits
enum DebugOverlayBits_t
{
	OVERLAY_TEXT_BIT			=	0x00000001,		// show text debug overlay for this entity
	OVERLAY_NAME_BIT			=	0x00000002,		// show name debug overlay for this entity
	OVERLAY_BBOX_BIT			=	0x00000004,		// show bounding box overlay for this entity
	OVERLAY_PIVOT_BIT			=	0x00000008,		// show pivot for this entity
	OVERLAY_MESSAGE_BIT			=	0x00000010,		// show messages for this entity
	OVERLAY_ABSBOX_BIT			=	0x00000020,		// show abs bounding box overlay
	OVERLAY_RBOX_BIT			=   0x00000040,     // show the rbox overlay
	OVERLAY_SHOW_BLOCKSLOS		=	0x00000080,		// show entities that block NPC LOS
	OVERLAY_ATTACHMENTS_BIT		=	0x00000100,		// show attachment points
	OVERLAY_AUTOAIM_BIT			=	0x00000200,		// Display autoaim radius

	OVERLAY_NPC_SELECTED_BIT	=	0x00001000,		// the npc is current selected
	OVERLAY_NPC_NEAREST_BIT		=	0x00002000,		// show the nearest node of this npc
	OVERLAY_NPC_ROUTE_BIT		=	0x00004000,		// draw the route for this npc
	OVERLAY_NPC_TRIANGULATE_BIT =	0x00008000,		// draw the triangulation for this npc
	OVERLAY_NPC_ZAP_BIT			=	0x00010000,		// destroy the NPC
	OVERLAY_NPC_ENEMIES_BIT		=	0x00020000,		// show npc's enemies
	OVERLAY_NPC_CONDITIONS_BIT	=	0x00040000,		// show NPC's current conditions
	OVERLAY_NPC_SQUAD_BIT		=	0x00080000,		// show npc squads
	OVERLAY_NPC_TASK_BIT		=	0x00100000,		// show npc task details
	OVERLAY_NPC_FOCUS_BIT		=	0x00200000,		// show line to npc's enemy and target
	OVERLAY_NPC_VIEWCONE_BIT	=	0x00400000,		// show npc's viewcone
	OVERLAY_NPC_KILL_BIT		=	0x00800000,		// kill the NPC, running all appropriate AI.

	OVERLAY_WC_CHANGE_ENTITY	=	0x01000000,		// object changed during WC edit
	OVERLAY_BUDDHA_MODE			=	0x02000000,		// take damage but don't die

	OVERLAY_NPC_STEERING_REGULATIONS	=	0x04000000,	// Show the steering regulations associated with the NPC

	OVERLAY_TASK_TEXT_BIT		=	0x08000000,		// show task and schedule names when they start

	OVERLAY_PROP_DEBUG			=	0x10000000,

	OVERLAY_NPC_RELATION_BIT	=	0x20000000,		// show relationships between target and all children

	OVERLAY_VIEWOFFSET			=	0x40000000,		// show view offset
};

struct TimedOverlay_t;

/* =========  CBaseEntity  ======== 

  All objects in the game are derived from this.

a list of all CBaseEntitys is kept in gEntList
================================ */

// creates an entity by string name, but does not spawn it
// If iForceEdictIndex is not -1, then it will use the edict by that index. If the index is 
// invalid or there is already an edict using that index, it will error out.
//CBaseEntity *CreateEntityByName( const char *className, int iForceEdictIndex = -1 );
//CBaseNetworkable *CreateNetworkableByName( const char *className );

// creates an entity and calls all the necessary spawn functions
//extern void SpawnEntityByName( const char *className, CEntityMapData *mapData = NULL );

// calls the spawn functions for an entity
extern int DispatchSpawn( CBaseEntity *pEntity );

//extern ISaveRestoreOps* engineObjectFuncs;
extern ISoundEmitterSystem* g_pSoundEmitterSystem;
extern CServerGameDLL g_ServerGameDLL;


//inline CBaseEntity *GetContainingEntity( edict_t *pent );



struct EmitSound_t;
struct rotatingpushmove_t;

class CEntityNetworkProperty : public CServerNetworkProperty {
public:
	void Init(CBaseEntity* pEntity);

	int			entindex() const;
	SendTable* GetSendTable();
	ServerClass* GetServerClass();
	void* GetDataTableBasePtr();

private:
	CBaseEntity* m_pOuter = NULL;;
};

//#define CREATE_PREDICTED_ENTITY( className )	\
//	CBaseEntity::CreatePredictedEntityByName( className, __FILE__, __LINE__ );

//
// Base Entity.  All entity types derive from this
//
class CBaseEntity : public IServerEntity
{
public:
	DECLARE_CLASS_NOBASE( CBaseEntity );	

	//----------------------------------------
	// Class vars and functions
	//----------------------------------------
	static inline void Debug_Pause(bool bPause);
	static inline bool Debug_IsPaused(void);
	static inline void Debug_SetSteps(int nSteps);
	static inline bool Debug_ShouldStep(void);
	static inline bool Debug_Step(void);

	static bool				m_bInDebugSelect;
	static int				m_nDebugPlayer;

protected:

	static bool				m_bDebugPause;		// Whether entity i/o is paused for debugging.
	static int				m_nDebugSteps;		// Number of entity outputs to fire before pausing again.

public:
	// If bServerOnly is true, then the ent never goes to the client. This is used
	// by logical entities.
	CBaseEntity();
	virtual ~CBaseEntity();

	// prediction system
	DECLARE_PREDICTABLE();
	// network data
	DECLARE_SERVERCLASS();
	// data description
	DECLARE_DATADESC();
	
	// memory handling
    void *operator new( size_t stAllocateBlock );
    void *operator new( size_t stAllocateBlock, int nBlockUse, const char *pFileName, int nLine );
	void operator delete( void *pMem );
	void operator delete( void *pMem, int nBlockUse, const char *pFileName, int nLine ) { operator delete(pMem); }

	// Class factory
	//static CBaseEntity				*CreatePredictedEntityByName( const char *classname, const char *module, int line, bool persist = false );

// IHandleEntity overrides.
public:
	//virtual void			SetRefEHandle( const CBaseHandle &handle );
	//virtual const			CBaseHandle& GetRefEHandle() const;

// IServerUnknown overrides
	virtual void					Release();
	virtual ICollideable	*GetCollideable();
	virtual IServerNetworkable *GetNetworkable();
	virtual CBaseEntity		*GetBaseEntity();

// IServerEntity overrides.
public:


	void					ClearModelIndexOverrides( void );
	virtual void			SetModelIndexOverride( int index, int nValue );

public:
	// virtual methods for derived classes to override
		// Called by physics to see if we should avoid a collision test....
	virtual	bool			ShouldCollide(int collisionGroup, int contentsMask) const;
	virtual bool			TestCollision( const Ray_t& ray, unsigned int mask, trace_t& trace );
	virtual	bool			TestHitboxes( const Ray_t &ray, unsigned int fContentsMask, trace_t& tr );
	virtual void			ComputeWorldSpaceSurroundingBox( Vector *pWorldMins, Vector *pWorldMaxs );

public:

	CEntityNetworkProperty *NetworkProp();
	const CEntityNetworkProperty *NetworkProp() const;

	void					SetNavIgnore( float duration = FLT_MAX );
	void					ClearNavIgnore();
	bool					IsNavIgnored() const;


	// virtual methods; you can override these
public:
	// Owner entity.
	// FIXME: These are virtual only because of CNodeEnt
	CBaseEntity				*GetOwnerEntity() const;
	virtual void			SetOwnerEntity( CBaseEntity* pOwner );
	void					SetEffectEntity( CBaseEntity *pEffectEnt );
	CBaseEntity				*GetEffectEntity() const;

	// Only CBaseEntity implements these. CheckTransmit calls the virtual ShouldTransmit to see if the
	// entity wants to be sent. If so, it calls SetTransmit, which will mark any dependents for transmission too.
	virtual int				ShouldTransmit( const CCheckTransmitInfo *pInfo );

	// update the global transmit state if a transmission rule changed
		    int				SetTransmitState( int nFlag);
			int				GetTransmitState( void );
	int						DispatchUpdateTransmitState();
	
	// Do NOT call this directly. Use DispatchUpdateTransmitState.
	virtual int				UpdateTransmitState();
	
	// Entities (like ropes) use this to own the transmit state of another entity
	// by forcing it to not call UpdateTransmitState.
	void					IncrementTransmitStateOwnedCounter();
	void					DecrementTransmitStateOwnedCounter();

	// This marks the entity for transmission and passes the SetTransmit call to any dependents.
	virtual void			SetTransmit( CCheckTransmitInfo *pInfo, bool bAlways );

	// This function finds out if the entity is in the 3D skybox. If so, it sets the EFL_IN_SKYBOX
	// flag so the entity gets transmitted to all the clients.
	// Entities usually call this during their Activate().
	// Returns true if the entity is in the skybox (and EFL_IN_SKYBOX was set).
	bool					DetectInSkybox();

	// Returns which skybox the entity is in
	CSkyCamera				*GetEntitySkybox();



public:

	virtual const char	*GetTracerType( void );

	// returns a pointer to the entities edict, if it has one.  should be removed!
	//inline edict_t			*edict( void )			{ return NetworkProp()->edict(); }
	//inline const edict_t	*edict( void ) const	{ return NetworkProp()->edict(); }
	//inline int				entindex( ) const		{
	//	CBaseHandle Handle = this->GetRefEHandle();
	//	if (Handle == INVALID_ENTITY_HANDLE) {
	//		return -1;
	//	}
	//	else {
	//		return Handle.GetEntryIndex();
	//	}
	//};

	// initialization
	virtual void Spawn( void );
	virtual void Precache( void ) {}


	virtual void SetModel( const char *szModelName );
public:
	// Notification on model load. May be called multiple times for dynamic models.
	// Implementations must call BaseClass::OnNewModel and pass return value through.
	virtual IStudioHdr *OnNewModel();

public:
	virtual void PostConstructor( const char *szClassname, int iForceEdictIndex);
	virtual void PostClientActive( void );
	virtual bool KeyValue( const char *szKeyName, const char *szValue );
	virtual bool KeyValue( const char *szKeyName, float flValue );
	virtual bool KeyValue( const char *szKeyName, const Vector &vecValue );
	virtual bool GetKeyValue( const char *szKeyName, char *szValue, int iMaxLen );

	void ValidateEntityConnections();
	void FireNamedOutput( const char *pszOutput, variant_t variant, CBaseEntity *pActivator, CBaseEntity *pCaller, float flDelay = 0.0f );

	// Activate - called for each entity after each load game and level load
	virtual void Activate( void );

	// Hierarchy traversal
	//CBaseEntity *GetMoveParent( void );
	//CBaseEntity *GetRootMoveParent();
	//CBaseEntity *FirstMoveChild( void );
	//CBaseEntity *NextMovePeer( void );

	//void		SetName( string_t newTarget );
	void SetName(const char* newName)
	{
		GetEngineObject()->SetName(newName);
	}
	void		SetParent( string_t newParent, CBaseEntity *pActivator, int iAttachment = -1 );
	
	
	virtual void	BeforeParentChanged(CBaseEntity* pNewParent, int inewAttachment = -1) {}
	virtual void	AfterParentChanged(CBaseEntity* pOldParent, int iOldAttachment = -1) {
		if (GetEngineObject()->GetMoveParent()) {
			// Move our step data into the correct space
			if (pOldParent == NULL)
			{
				// Transform step data from world to parent-space
				TransformStepData_WorldToParent(this);
			}
			else
			{
				// Transform step data between parent-spaces
				TransformStepData_ParentToParent(pOldParent, this);
			}
		}
		else {
			// Transform step data from parent to worldspace
			TransformStepData_ParentToWorld(pOldParent);
		}
	}

	//CBaseEntity* GetParent();
	//int			GetParentAttachment();

	//string_t	GetEntityName();
	//-----------------------------------------------------------------------------
// Inline methods
//-----------------------------------------------------------------------------
	string_t GetEntityName()
	{
		return GetEngineObject()->GetEntityName();
	}
	

	bool		NameMatches(const char* pszNameOrWildcard) {
		return GetEngineObject()->NameMatches(pszNameOrWildcard);
	}
	bool		ClassMatches(const char* pszClassOrWildcard) {
		return GetEngineObject()->ClassMatches(pszClassOrWildcard);
	}
	bool		NameMatches(string_t nameStr) {
		return GetEngineObject()->NameMatches(nameStr);
	}
	bool		ClassMatches(string_t nameStr) {
		return GetEngineObject()->ClassMatches(nameStr);
	}


public:
	void		TransformStepData_WorldToParent( CBaseEntity *pParent );
	void		TransformStepData_ParentToParent( CBaseEntity *pOldParent, CBaseEntity *pNewParent );
	void		TransformStepData_ParentToWorld( CBaseEntity *pParent );


public:

	virtual void OnAddEffects(int nEffects) {}
	virtual void OnRemoveEffects(int nEffects) {}
	// makes the entity inactive
	void		MakeDormant( void );
	int			IsDormant( void );

	void		RemoveDeferred( void );	// Sets the entity invisible, and makes it remove itself on the next frame



	// capabilities
	virtual int	ObjectCaps( void );

	// handles an input (usually caused by outputs)
	// returns true if the the value in the pass in should be set, false if the input is to be ignored
	virtual bool AcceptInput( const char *szInputName, CBaseEntity *pActivator, CBaseEntity *pCaller, variant_t Value, int outputID );

	//
	// Input handlers.
	//
	void InputAlternativeSorting( inputdata_t &inputdata );
	void InputAlpha( inputdata_t &inputdata );
	void InputColor( inputdata_t &inputdata );
	void InputSetParent( inputdata_t &inputdata );
	void SetParentAttachment( const char *szInputName, const char *szAttachment, bool bMaintainOffset );
	void InputSetParentAttachment( inputdata_t &inputdata );
	void InputSetParentAttachmentMaintainOffset( inputdata_t &inputdata );
	void InputClearParent( inputdata_t &inputdata );
	void InputSetTeam( inputdata_t &inputdata );
	void InputUse( inputdata_t &inputdata );
	void InputKill( inputdata_t &inputdata );
	void InputKillHierarchy( inputdata_t &inputdata );
	void InputSetDamageFilter( inputdata_t &inputdata );
	void InputDispatchEffect( inputdata_t &inputdata );
	void InputEnableDamageForces( inputdata_t &inputdata );
	void InputDisableDamageForces( inputdata_t &inputdata );
	void InputAddContext( inputdata_t &inputdata );
	void InputRemoveContext( inputdata_t &inputdata );
	void InputClearContext( inputdata_t &inputdata );
	void InputDispatchResponse( inputdata_t& inputdata );
	void InputDisableShadow( inputdata_t &inputdata );
	void InputEnableShadow( inputdata_t &inputdata );
	void InputAddOutput( inputdata_t &inputdata );
	void InputFireUser1( inputdata_t &inputdata );
	void InputFireUser2( inputdata_t &inputdata );
	void InputFireUser3( inputdata_t &inputdata );
	void InputFireUser4( inputdata_t &inputdata );

	// Fire
	virtual void Ignite(float flFlameLifetime, bool bNPCOnly = true, float flSize = 0.0f, bool bCalledByLevelDesigner = false);
	virtual void IgniteLifetime(float flFlameLifetime);
	virtual void IgniteNumHitboxFires(int iNumHitBoxFires);
	virtual void IgniteHitboxFireScale(float flHitboxFireScale);
	virtual void Extinguish() { GetEngineObject()->RemoveFlag(FL_ONFIRE); }
	bool IsOnFire() { return ((GetEngineObject()->GetFlags() & FL_ONFIRE) != 0); }
	void Scorch(int rate, int floor);
	void InputIgnite(inputdata_t& inputdata);
	void InputIgniteLifetime(inputdata_t& inputdata);
	void InputIgniteNumHitboxFires(inputdata_t& inputdata);
	void InputIgniteHitboxFireScale(inputdata_t& inputdata);
	void InputSetLightingOriginRelative(inputdata_t& inputdata);
	void InputSetLightingOrigin(inputdata_t& inputdata);

	// Dissolve, returns true if the ragdoll has been created
	bool Dissolve(const char* pMaterialName, float flStartTime, bool bNPCOnly = true, int nDissolveType = 0, Vector vDissolverOrigin = vec3_origin, int iMagnitude = 0);
	bool IsDissolving() { return ((GetEngineObject()->GetFlags() & FL_DISSOLVING) != 0); }
	void TransferDissolveFrom(CBaseEntity* pAnim);

	// Returns the origin at which to play an inputted dispatcheffect 
	virtual void GetInputDispatchEffectPosition( const char *sInputString, Vector &pOrigin, QAngle &pAngles );

	// tries to read a field from the entities data description - result is placed in variant_t
	bool ReadKeyField( const char *varName, variant_t *var );

	// classname access
	void SetClassname(const char* className)
	{
		GetEngineObject()->SetClassname(className);
	}
	const char* GetClassName() const
	{
		return GetEngineObject()->GetClassName();
	}
	const char* GetClassname() const
	{
		return STRING( GetEngineObject()->GetClassname());
	}

	// Debug Overlays
	void		 EntityText( int text_offset, const char *text, float flDuration, int r = 255, int g = 255, int b = 255, int a = 255 );
	const char	*GetDebugName(void); // do not make this virtual -- designed to handle NULL this
	virtual	void DrawDebugGeometryOverlays(void);					
	virtual int  DrawDebugTextOverlays(void);
	void		 DrawTimedOverlays( void );
	void		 DrawBBoxOverlay( float flDuration = 0.0f );
	void		 DrawAbsBoxOverlay();
	void		 DrawRBoxOverlay();

	void		 DrawInputOverlay(const char *szInputName, CBaseEntity *pCaller, variant_t Value);
	void		 DrawOutputOverlay(CEventAction *ev);
	void		 SendDebugPivotOverlay( void );
	void		 AddTimedOverlay( const char *msg, int endTime );
	void	SetFadeDistance(float minFadeDist, float maxFadeDist);


	// save/restore
	// only overload these if you have special data to serialize
	virtual int	Save( ISave &save ) 
	{ 
		return GetEngineObject()->Save(save);
	}
	virtual int	Restore( IRestore &restore ) 
	{ 
		int status= GetEngineObject()->Restore(restore);
		return status;
	}
	virtual bool ShouldSavePhysics();

	// handler to reset stuff before you are restored
	// NOTE: Always chain to base class when implementing this!
	virtual void OnSave( IEntitySaveUtils *pSaveUtils );

	// handler to reset stuff after you are restored
	// called after all entities have been loaded from all affected levels
	// called before activate
	// NOTE: Always chain to base class when implementing this!
	virtual void OnRestore();

	int			 GetTextureFrameIndex( void );
	void		 SetTextureFrameIndex( int iIndex );

	// Entities block Line-Of-Sight for NPCs by default.
	// Set this to false if you want to change this behavior.
	void		 SetBlocksLOS( bool bBlocksLOS );
	bool		 BlocksLOS( void );


	void		 SetAIWalkable( bool bBlocksLOS );
	bool		 IsAIWalkable( void );
private:
	//int SaveDataDescBlock( ISave &save, datamap_t *dmap );
	//int RestoreDataDescBlock( IRestore &restore, datamap_t *dmap );

public:
	// Networking related methods
	void	NetworkStateChanged();
	void	NetworkStateChanged( void *pVar );
	void	NetworkStateChanged(unsigned short varOffset);
	
public:

	// returns the edict index the entity requires when used in save/restore (eg players, world)
	// -1 means it doesn't require any special index
	static int RequiredEdictIndexStatic( void ) { return -1; } 
	virtual int RequiredEdictIndex(void) { return CBaseEntity::RequiredEdictIndexStatic(); }
	static bool IsNetworkableStatic(void) { return true; }
	virtual bool IsNetworkable(void) { return CBaseEntity::IsNetworkableStatic(); }

	// interface function pts
	void (CBaseEntity::*m_pfnMoveDone)(void);
	virtual void MoveDone( void ) { if (m_pfnMoveDone) (this->*m_pfnMoveDone)();};

	// Why do we have two separate static Instance functions?
	static CBaseEntity *Instance( const CBaseHandle &hEnt );
	//static CBaseEntity *Instance( const edict_t *pent );
	//static CBaseEntity *Instance( edict_t *pent );
	static CBaseEntity* Instance( int iEnt );

	
	virtual void Think( void ) { 
		if (GetEngineObject()->GetPfnThink()) 
			(this->*GetEngineObject()->GetPfnThink())();
	};





	bool				IsTransparent() const;
	void				SetRenderMode( RenderMode_t nRenderMode );
	RenderMode_t		GetRenderMode() const;

private:
	// NOTE: Keep this near vtable so it's in cache with vtable.
	CEntityNetworkProperty m_Network;

public:

	int		m_iHammerID; // Hammer unique edit id number

public:
	// was pev->speed
	float		m_flSpeed;
	// was pev->renderfx
	CNetworkVar( unsigned char, m_nRenderFX );
	// was pev->rendermode
	CNetworkVar( unsigned char, m_nRenderMode );
	
#ifdef TF_DLL
	CNetworkArray( int, m_nModelIndexOverrides, MAX_VISION_MODES ); // used to override the base model index on the client if necessary
#endif

	// was pev->rendercolor
	CNetworkColor32( m_clrRender );
	const color32 GetRenderColor() const;
	void SetRenderColor( byte r, byte g, byte b );
	void SetRenderColor( byte r, byte g, byte b, byte a );
	void SetRenderColorR( byte r );
	void SetRenderColorG( byte g );
	void SetRenderColorB( byte b );
	void SetRenderColorA( byte a );


	// was pev->animtime:  consider moving to CBaseAnimating
	float		m_flPrevAnimTime;


	void IncrementInterpolationFrame(); // Call this to cause a discontinuity (teleport)

	CNetworkVar( int, m_ubInterpolationFrame );


//#if !defined( NO_ENTITY_PREDICTION )
//	// Certain entities (projectiles) can be created on the client and thus need a matching id number
//	CNetworkVar( CPredictableId, m_PredictableID );
//#endif

	void OnResetSequence(int nSequence) {}

protected:



	void RemoveExpiredConcepts( void );
	int	GetContextCount() const;						// Call RemoveExpiredConcepts to clean out expired concepts
	const char *GetContextName( int index ) const;		// note: context may be expired
	const char *GetContextValue( int index ) const; 	// note: context may be expired
	bool ContextExpired( int index ) const;
	int FindContextByName( const char *name ) const;
public:
	void	AddContext( const char *nameandvalue );

protected:
	CUtlVector< ResponseContext_t > m_ResponseContexts;

	// Map defined context sets
	string_t	m_iszResponseContext;

private:
	CBaseEntity( CBaseEntity& );

	// list handling
	template<class T>
	friend class CGlobalEntityList;
	friend class CThinkSyncTester;

	


////////////////////////////////////////////////////////////////////////////


public:

	// Returns a CBaseAnimating if the entity is derived from CBaseAnimating.
	virtual CBaseAnimating*	GetBaseAnimating() { return 0; }

	virtual IResponseSystem *GetResponseSystem();
	virtual void	DispatchResponse( const char *conceptName );

// Classify - returns the type of group (i.e, "houndeye", or "human military" so that NPCs with different classnames
// still realize that they are teammates. (overridden for NPCs that form groups)
	virtual Class_T Classify ( void );
	virtual void	DeathNotice ( CBaseEntity *pVictim ) {}// NPC maker children use this to tell the NPC maker that they have died.
	virtual bool	ShouldAttractAutoAim( CBaseEntity *pAimingEnt ) { return ((GetEngineObject()->GetFlags() & FL_AIMTARGET) != 0); }
	virtual float	GetAutoAimRadius();
	virtual Vector	GetAutoAimCenter() { return WorldSpaceCenter(); }

	virtual ITraceFilter*	GetBeamTraceFilter( void );

	// Call this to do a TraceAttack on an entity, performs filtering. Don't call TraceAttack() directly except when chaining up to base class
	void			DispatchTraceAttack( const CTakeDamageInfo &info, const Vector &vecDir, trace_t *ptr, CDmgAccumulator *pAccumulator = NULL );
	virtual bool	PassesDamageFilter( const CTakeDamageInfo &info );


protected:
	virtual void	TraceAttack( const CTakeDamageInfo &info, const Vector &vecDir, trace_t *ptr, CDmgAccumulator *pAccumulator = NULL );

public:

	virtual bool	CanBeHitByMeleeAttack( CBaseEntity *pAttacker ) { return true; }

	// returns the amount of damage inflicted
	virtual int		OnTakeDamage( const CTakeDamageInfo &info );

	// This is what you should call to apply damage to an entity.
	void TakeDamage( const CTakeDamageInfo &info );
	virtual void AdjustDamageDirection( const CTakeDamageInfo &info, Vector &dir, CBaseEntity *pEnt ) {}

	virtual int		TakeHealth( float flHealth, int bitsDamageType );

	virtual bool	IsAlive( void );
	// Entity killed (only fired once)
	virtual void	Event_Killed( const CTakeDamageInfo &info );
	
	void SendOnKilledGameEvent( const CTakeDamageInfo &info );

	// Notifier that I've killed some other entity. (called from Victim's Event_Killed).
	virtual void	Event_KilledOther( CBaseEntity *pVictim, const CTakeDamageInfo &info ) { return; }

	// UNDONE: Make this data?
	virtual int				BloodColor( void );

	void					TraceBleed( float flDamage, const Vector &vecDir, trace_t *ptr, int bitsDamageType );
	virtual bool			IsTriggered( CBaseEntity *pActivator ) {return true;}
	virtual bool			IsNPC( void ) const { return false; }
	CAI_BaseNPC				*MyNPCPointer( void ); 
	virtual CBaseCombatCharacter *MyCombatCharacterPointer( void ) { return NULL; }
	virtual INextBot		*MyNextBotPointer( void ) { return NULL; }
	virtual float			GetDelay( void ) { return 0; }
	virtual bool			IsMoving( void );
	bool					IsWorld() { return entindex() == 0; }
	virtual char const		*DamageDecal( int bitsDamageType, int gameMaterial );
	virtual void			DecalTrace( trace_t *pTrace, char const *decalName );
	virtual void			ImpactTrace( trace_t *pTrace, int iDamageType, const char *pCustomImpactName = NULL );

	void			AddPoints( int score, bool bAllowNegativeScore );
	void			AddPointsToTeam( int score, bool bAllowNegativeScore );
	void			RemoveAllDecals( void );

	virtual bool	OnControls( CBaseEntity *pControls ) { return false; }
	virtual bool	HasTarget( string_t targetname );
	virtual	bool	IsPlayer( void ) const { return false; }
	virtual bool	IsNetClient( void ) const { return false; }
	virtual bool	IsTemplate( void ) { return false; }
	virtual bool	IsBaseObject( void ) const { return false; }
	virtual bool	IsBaseTrain( void ) const { return false; }
	bool			IsBSPModel() const;
	bool			IsCombatCharacter() { return MyCombatCharacterPointer() == NULL ? false : true; }
	bool			IsInWorld( void ) const;
	virtual bool	IsCombatItem( void ) const { return false; }
	virtual bool	IsViewModel() { return false; }

	virtual bool	IsBaseCombatWeapon( void ) const { return false; }
	virtual bool	IsWearable( void ) const { return false; }
	virtual CBaseCombatWeapon *MyCombatWeaponPointer( void ) { return NULL; }

	// If this is a vehicle, returns the vehicle interface
	virtual IServerVehicle*			GetServerVehicle() { return NULL; }

	// UNDONE: Make this data instead of procedural?
	virtual bool	IsViewable( void );					// is this something that would be looked at (model, sprite, etc.)?
	
	virtual bool	IsChangeLevelTrigger() { return false; };
	virtual const char* GetNewLandmarkName() { return ""; };
	virtual const char* GetNewMapName() { return ""; };

	// Team Handling
	CTeam			*GetTeam( void ) const;				// Get the Team this entity is on
	int				GetTeamNumber( void ) const;		// Get the Team number of the team this entity is on
	virtual void	ChangeTeam( int iTeamNum );			// Assign this entity to a team.
	bool			IsInTeam( CTeam *pTeam ) const;		// Returns true if this entity's in the specified team
	bool			InSameTeam( CBaseEntity *pEntity ) const;	// Returns true if the specified entity is on the same team as this one
	bool			IsInAnyTeam( void ) const;			// Returns true if this entity is in any team
	const char		*TeamID( void ) const;				// Returns the name of the team this entity is on.

	// Entity events... these are events targetted to a particular entity
	// Each event defines its own well-defined event data structure
	virtual void OnEntityEvent( EntityEvent_t event, void *pEventData );

	// can stand on this entity?
	bool IsStandable() const;

	// UNDONE: Do these three functions actually need to be virtual???
	virtual bool	CanStandOn( CBaseEntity *pSurface ) const { return (pSurface && !pSurface->IsStandable()) ? false : true; }
	//virtual bool	CanStandOn( edict_t	*ent ) const { return CanStandOn( GetContainingEntity( ent ) ); }
	virtual CBaseEntity		*GetEnemy( void ) { return NULL; }
	virtual CBaseEntity		*GetEnemy( void ) const { return NULL; }


	void	ViewPunch( const QAngle &angleOffset );
	void	VelocityPunch( const Vector &vecForce );

	CBaseEntity *GetNextTarget( void );
	
	// fundamental callbacks
	void (CBaseEntity ::*m_pfnTouch)( CBaseEntity *pOther );
	void (CBaseEntity ::*m_pfnUse)( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );
	void (CBaseEntity ::*m_pfnBlocked)( CBaseEntity *pOther );

	virtual void			Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );
	virtual void			StartTouch( CBaseEntity *pOther );
	virtual void			Touch( CBaseEntity *pOther ); 
	virtual void			EndTouch( CBaseEntity *pOther );
	virtual void			StartBlocked( CBaseEntity *pOther ) {}
	virtual void			Blocked( CBaseEntity *pOther );
	virtual void			EndBlocked( void ) {}

	// Physics simulation
	virtual void			PhysicsSimulate( void );

public:

	void					StartGroundContact( CBaseEntity *ground );
	void					EndGroundContact( CBaseEntity *ground );



	// Remove this as ground entity for all object resting on this object
	void					WakeRestingObjects();
	bool					HasNPCsOnIt();

	virtual void			UpdateOnRemove( void );
	virtual void			StopLoopingSounds( void ) {}

	// common member functions
	void					SUB_Remove( void );
	void					SUB_DoNothing( void );
	void					SUB_StartFadeOut( float delay = 10.0f, bool bNotSolid = true );
	void					SUB_StartFadeOutInstant();
	void					SUB_FadeOut ( void );
	void					SUB_Vanish( void );
	void					SUB_CallUseToggle( void ) { this->Use( this, this, USE_TOGGLE, 0 ); }
	void					SUB_PerformFadeOut( void );
	virtual	bool			SUB_AllowedToFade( void );

	// change position, velocity, orientation instantly
	// passing NULL means no change
	virtual void			Teleport( const Vector *newPosition, const QAngle *newAngles, const Vector *newVelocity );
	// notify that another entity (that you were watching) was teleported
	virtual void			NotifySystemEvent( CBaseEntity *pNotify, notify_system_event_t eventType, const notify_system_event_params_t &params );

	int						ShouldToggle( USE_TYPE useType, int currentState );

	// UNDONE: Move these virtuals to CBaseCombatCharacter?
	virtual void MakeTracer( const Vector &vecTracerSrc, const trace_t &tr, int iTracerType );
	virtual int	GetTracerAttachment( void );
	virtual void FireBullets( const FireBulletsInfo_t &info );
	virtual void DoImpactEffect( trace_t &tr, int nDamageType ); // give shooter a chance to do a custom impact.

	// OLD VERSION! Use the struct version
	void FireBullets( int cShots, const Vector &vecSrc, const Vector &vecDirShooting, 
		const Vector &vecSpread, float flDistance, int iAmmoType, int iTracerFreq = 4, 
		int firingEntID = -1, int attachmentID = -1, int iDamage = 0, 
		CBaseEntity *pAttacker = NULL, bool bFirstShotAccurate = false, bool bPrimaryAttack = true );
	virtual void ModifyFireBulletsDamage( CTakeDamageInfo* dmgInfo ) {}

	virtual CBaseEntity *Respawn( void ) { return NULL; }

	// Method used to deal with attacks passing through triggers
	void TraceAttackToTriggers( const CTakeDamageInfo &info, const Vector& start, const Vector& end, const Vector& dir );

	virtual bool IsLockedByMaster( void ) { return false; }

	// Health accessors.
	virtual int		GetMaxHealth()  const	{ return m_iMaxHealth; }
	void	SetMaxHealth( int amt )	{ m_iMaxHealth = amt; }

	int		GetHealth() const		{ return m_iHealth; }
	void	SetHealth( int amt )	{ m_iHealth = amt; }

	// Ugly code to lookup all functions to make sure they are in the table when set.
#ifdef _DEBUG

#ifdef PLATFORM_64BITS
#ifdef GNUC
#define ENTITYFUNCPTR_SIZE	16
#else
#define ENTITYFUNCPTR_SIZE	8
#endif
#else
#ifdef GNUC
#define ENTITYFUNCPTR_SIZE	8
#else
#define ENTITYFUNCPTR_SIZE	4
#endif
#endif

	void FunctionCheck( void *pFunction, const char *name );

	ENTITYFUNCPTR TouchSet( ENTITYFUNCPTR func, char *name ) 
	{ 
#ifdef _DEBUG
#ifdef PLATFORM_64BITS
#ifdef GNUC
	COMPILE_TIME_ASSERT( sizeof(func) == 16 );
#else
	COMPILE_TIME_ASSERT( sizeof(func) == 8 );
#endif
#else
#ifdef GNUC
	COMPILE_TIME_ASSERT( sizeof(func) == 8 );
#else
	COMPILE_TIME_ASSERT( sizeof(func) == 4 );
#endif
#endif
#endif
		m_pfnTouch = func; 
		FunctionCheck( *(reinterpret_cast<void **>(&m_pfnTouch)), name ); 
		return func;
	}
	USEPTR	UseSet( USEPTR func, char *name ) 
	{ 
#ifdef _DEBUG
#ifdef PLATFORM_64BITS
#ifdef GNUC
	COMPILE_TIME_ASSERT( sizeof(func) == 16 );
#else
	COMPILE_TIME_ASSERT( sizeof(func) == 8 );
#endif
#else
#ifdef GNUC
	COMPILE_TIME_ASSERT( sizeof(func) == 8 );
#else
	COMPILE_TIME_ASSERT( sizeof(func) == 4 );
#endif
#endif
#endif
		m_pfnUse = func; 
		FunctionCheck( *(reinterpret_cast<void **>(&m_pfnUse)), name ); 
		return func;
	}
	ENTITYFUNCPTR	BlockedSet( ENTITYFUNCPTR func, char *name ) 
	{ 
#ifdef _DEBUG
#ifdef PLATFORM_64BITS
#ifdef GNUC
	COMPILE_TIME_ASSERT( sizeof(func) == 16 );
#else
	COMPILE_TIME_ASSERT( sizeof(func) == 8 );
#endif
#else
#ifdef GNUC
	COMPILE_TIME_ASSERT( sizeof(func) == 8 );
#else
	COMPILE_TIME_ASSERT( sizeof(func) == 4 );
#endif
#endif
#endif
		m_pfnBlocked = func; 
		FunctionCheck( *(reinterpret_cast<void **>(&m_pfnBlocked)), name ); 
		return func;
	}

#endif
	virtual void	ModifyOrAppendCriteria( AI_CriteriaSet& set );
	void			AppendContextToCriteria( AI_CriteriaSet& set, const char *prefix = "" );
	void			DumpResponseCriteria( void );
	
private:
	friend class CAI_Senses;
	CBaseEntity	*m_pLink;// used for temporary link-list operations. 

public:
	// variables promoted from edict_t
	string_t	m_target;
	CNetworkVarForDerived( int, m_iMaxHealth ); // CBaseEntity doesn't care about changes to this variable, but there are derived classes that do.
	CNetworkVarForDerived( int, m_iHealth );

	CNetworkVarForDerived( char, m_lifeState );
	CNetworkVarForDerived( char , m_takedamage );

	// Damage filtering
	string_t	m_iszDamageFilterName;	// The name of the entity to use as our damage filter.
	EHANDLE		m_hDamageFilter;		// The entity that controls who can damage us.

	// Debugging / devolopment fields
	int				m_debugOverlays;	// For debug only (bitfields)
	TimedOverlay_t*	m_pTimedOverlay;	// For debug only

	// virtual functions used by a few classes
	
	// creates an entity of a specified class, by name
	static CBaseEntity *Create( const char *szName, const Vector &vecOrigin, const QAngle &vecAngles, CBaseEntity *pOwner = NULL );
	static CBaseEntity *CreateNoSpawn( const char *szName, const Vector &vecOrigin, const QAngle &vecAngles, CBaseEntity *pOwner = NULL );

	// Damage accessors
	virtual int		GetDamageType() const;
	virtual float	GetDamage() { return 0; }
	virtual void	SetDamage(float flDamage) {}

	virtual Vector	EyePosition( void );			// position of eyes
	virtual const QAngle &EyeAngles( void );		// Direction of eyes in world space
	virtual const QAngle &LocalEyeAngles( void );	// Direction of eyes
	virtual Vector	EarPosition( void );			// position of ears

	Vector	EyePosition( void ) const;			// position of eyes
	const QAngle &EyeAngles( void ) const;		// Direction of eyes in world space
	const QAngle &LocalEyeAngles( void ) const;	// Direction of eyes
	Vector	EarPosition( void ) const;			// position of ears

	virtual Vector	BodyTarget( const Vector &posSrc, bool bNoisy = true);		// position to shoot at
	virtual Vector	HeadTarget( const Vector &posSrc );
	virtual void	GetVectors(Vector* forward, Vector* right, Vector* up) const;

	virtual const Vector &GetViewOffset() const;
	virtual void SetViewOffset( const Vector &v );

	//can not call this in constructor!
	virtual IEngineObjectServer* GetEngineObject();
	virtual const IEngineObjectServer* GetEngineObject() const;
	// NOTE: Setting the abs velocity in either space will cause a recomputation
	// in the other space, so setting the abs velocity will also set the local vel
	void			ApplyLocalVelocityImpulse( const Vector &vecImpulse );
	void			ApplyAbsVelocityImpulse( const Vector &vecImpulse );
	void			ApplyLocalAngularVelocityImpulse( const AngularImpulse &angImpulse );

	

	// NOTE: Setting the abs velocity in either space will cause a recomputation
	// in the other space, so setting the abs velocity will also set the local vel
	void			SetLocalAngularVelocity( const QAngle &vecAngVelocity );
	const QAngle&	GetLocalAngularVelocity( ) const;

	// FIXME: While we're using (dPitch, dYaw, dRoll) as our local angular velocity
	// representation, we can't actually solve this problem
//	void			SetAbsAngularVelocity( const QAngle &vecAngVelocity );
//	const QAngle&	GetAbsAngularVelocity( ) const;

	const Vector&	GetBaseVelocity() const;
	void			SetBaseVelocity( const Vector& v );

	virtual Vector	GetSmoothedVelocity( void );

	// FIXME: Figure out what to do about this
	virtual void	GetVelocity(Vector *vVelocity, AngularImpulse *vAngVelocity = NULL);




	virtual	bool FVisible ( CBaseEntity *pEntity, int traceMask = MASK_BLOCKLOS, CBaseEntity **ppBlocker = NULL );
	virtual bool FVisible( const Vector &vecTarget, int traceMask = MASK_BLOCKLOS, CBaseEntity **ppBlocker = NULL );

	virtual bool CanBeSeenBy( CAI_BaseNPC *pNPC ) { return true; } // allows entities to be 'invisible' to NPC senses.

	// This function returns a value that scales all damage done by this entity.
	// Use CDamageModifier to hook in damage modifiers on a guy.
	virtual float			GetAttackDamageScale( CBaseEntity *pVictim );
	// This returns a value that scales all damage done to this entity
	// Use CDamageModifier to hook in damage modifiers on a guy.
	virtual float			GetReceivedDamageScale( CBaseEntity *pAttacker );

	// Gets the velocity we impart to a player standing on us
	virtual void			GetGroundVelocityToApply( Vector &vecGroundVel ) { vecGroundVel = vec3_origin; }

	int						GetWaterLevel() const;
	void					SetWaterLevel( int nLevel );
	int						GetWaterType() const;
	void					SetWaterType( int nType );

	virtual bool			PhysicsSplash( const Vector &centerPoint, const Vector &normal, float rawSpeed, float scaledSpeed ) { return false; }
	virtual void			Splash() {}


	
	// These methods return a *world-aligned* box relative to the absorigin of the entity.
	// This is used for collision purposes and is *not* guaranteed
	// to surround the entire entity's visual representation
	// NOTE: It is illegal to ask for the world-aligned bounds for
	// SOLID_BSP objects
	const Vector&			WorldAlignMins( ) const;
	const Vector&			WorldAlignMaxs( ) const;



	// NOTE: The world space center *may* move when the entity rotates.
	virtual const Vector&	WorldSpaceCenter( ) const;
 	const Vector&			WorldAlignSize( ) const;

	// Returns a radius of a sphere 
	// *centered at the world space center* bounding the collision representation 
	// of the entity. NOTE: The world space center *may* move when the entity rotates.
	//float					BoundingRadius() const;

	



	void					SetShadowCastDistance( float flDistance );
	float					GetShadowCastDistance( void ) const;
	void					SetShadowCastDistance( float flDesiredDistance, float flDelay );

	float					GetLocalTime( void ) const;
	void					IncrementLocalTime( float flTimeDelta );
	float					GetMoveDoneTime( ) const;
	void					SetMoveDoneTime( float flTime );
	
	// Used by the PAS filters to ask the entity where in world space the sounds it emits come from.
	// This is used right now because if you have something sitting on an incline, using our axis-aligned 
	// bounding boxes can return a position in solid space, so you won't hear sounds emitted by the object.
	// For now, we're hacking around it by moving the sound emission origin up on certain objects like vehicles.
	//
	// When OBBs get in, this can probably go away.
	virtual Vector			GetSoundEmissionOrigin() const;
	inline int				GetSoundSourceIndex() const { return entindex(); }

	// Sets the local position from a transform
	void					SetLocalTransform( const matrix3x4_t &localTransform );

	// See CSoundEmitterSystem
	//static void					EmitSound(CBaseEntity* pEntity, const char *soundname, float soundtime = 0.0f, float *duration = NULL );  // Override for doing the general case of CPASAttenuationFilter filter( this ), and EmitSound( filter, entindex(), etc. );
	//static void					EmitSound(CBaseEntity* pEntity, const char *soundname, HSOUNDSCRIPTHANDLE& handle, float soundtime = 0.0f, float *duration = NULL );  // Override for doing the general case of CPASAttenuationFilter filter( this ), and EmitSound( filter, entindex(), etc. );
	//static void					StopSound(CBaseEntity* pEntity, const char *soundname );
	//static void					StopSound(CBaseEntity* pEntity, const char *soundname, HSOUNDSCRIPTHANDLE& handle );
	//static void					GenderExpandString(CBaseEntity* pEntity, char const *in, char *out, int maxlen );

	//virtual void ModifyEmitSoundParams( EmitSound_t &params );

	//static float GetSoundDuration( const char *soundname, char const *actormodel );

	//static bool	GetParametersForSound( const char *soundname, CSoundParameters &params, char const *actormodel );
	//static bool	GetParametersForSound( const char *soundname, HSOUNDSCRIPTHANDLE& handle, CSoundParameters &params, char const *actormodel );

	//static void EmitSound( IRecipientFilter& filter, int iEntIndex, const char *soundname, const Vector *pOrigin = NULL, float soundtime = 0.0f, float *duration = NULL );
	//static void EmitSound( IRecipientFilter& filter, int iEntIndex, const char *soundname, HSOUNDSCRIPTHANDLE& handle, const Vector *pOrigin = NULL, float soundtime = 0.0f, float *duration = NULL );
	//static void StopSound( int iEntIndex, const char *soundname );
	//static soundlevel_t LookupSoundLevel( const char *soundname );
	//static soundlevel_t LookupSoundLevel( const char *soundname, HSOUNDSCRIPTHANDLE& handle );

	//static void EmitSound( IRecipientFilter& filter, int iEntIndex, const EmitSound_t & params );
	//static void EmitSound( IRecipientFilter& filter, int iEntIndex, const EmitSound_t & params, HSOUNDSCRIPTHANDLE& handle );

	//static void StopSound( int iEntIndex, int iChannel, const char *pSample );

	//static void EmitAmbientSound( int entindex, const Vector& origin, const char *soundname, int flags = 0, float soundtime = 0.0f, float *duration = NULL );

	// These files need to be listed in scripts/game_sounds_manifest.txt
	//static HSOUNDSCRIPTHANDLE PrecacheScriptSound( const char *soundname );
	//static void PrefetchScriptSound( const char *soundname );

	// For each client who appears to be a valid recipient, checks the client has disabled CC and if so, removes them from 
	//  the recipient list.
	//static void RemoveRecipientsIfNotCloseCaptioning( CRecipientFilter& filter );
	//static void EmitCloseCaption( IRecipientFilter& filter, int entindex, char const *token, CUtlVector< Vector >& soundorigins, float duration, bool warnifmissing = false );
	static void	EmitSentenceByIndex( IRecipientFilter& filter, int iEntIndex, int iChannel, int iSentenceIndex, 
		float flVolume, soundlevel_t iSoundlevel, int iFlags = 0, int iPitch = PITCH_NORM,
		const Vector *pOrigin = NULL, const Vector *pDirection = NULL, bool bUpdatePositions = true, float soundtime = 0.0f );

	//static bool IsPrecacheAllowed();
	//static void SetAllowPrecache( bool allow );

	//static bool m_bAllowPrecache;

	static bool IsSimulatingOnAlternateTicks();

	virtual bool IsDeflectable() { return false; }
	virtual void Deflected( CBaseEntity *pDeflectedBy, Vector &vecDir ) {}

//	void Relink() {}

public:

	// VPHYSICS Integration -----------------------------------------------
	//
	// --------------------------------------------------------------------
	// UNDONE: Move to IEntityVPhysics? or VPhysicsProp() ?
	// Called after spawn, and in the case of self-managing objects, after load
	virtual bool	CreateVPhysics();

	// Convenience routines to init the vphysics simulation for this object.
	// This creates a static object.  Something that behaves like world geometry - solid, but never moves
	IPhysicsObject *VPhysicsInitStatic( void );

	// This creates a normal vphysics simulated object - physics determines where it goes (gravity, friction, etc)
	// and the entity receives updates from vphysics.  SetAbsOrigin(), etc do not affect the object!
	IPhysicsObject *VPhysicsInitNormal( SolidType_t solidType, int nSolidFlags, bool createAsleep, solid_t *pSolid = NULL );

	// This creates a vphysics object with a shadow controller that follows the AI
	// Move the object to where it should be and call UpdatePhysicsShadowToCurrentPosition()
	IPhysicsObject *VPhysicsInitShadow( bool allowPhysicsMovement, bool allowPhysicsRotation, solid_t *pSolid = NULL );

	// Force a non-solid (ie. solid_trigger) physics object to collide with other entities.
	virtual bool	ForceVPhysicsCollide( CBaseEntity *pEntity ) { return false; }

private:
	// called by all vphysics inits
	bool			VPhysicsInitSetup();
public:

	void			VPhysicsSetObject( IPhysicsObject *pPhysics );
	// destroy and remove the physics object for this entity
	virtual void	VPhysicsDestroyObject( void );
	void			VPhysicsSwapObject( IPhysicsObject *pSwap );

	inline IPhysicsObject *VPhysicsGetObject( void ) const { return m_pPhysicsObject; }
	virtual void	VPhysicsUpdate( IPhysicsObject *pPhysics );
	void			VPhysicsUpdatePusher( IPhysicsObject *pPhysics );
	
	// react physically to damage (called from CBaseEntity::OnTakeDamage() by default)
	virtual int		VPhysicsTakeDamage( const CTakeDamageInfo &info );
	virtual void	VPhysicsShadowCollision( int index, gamevcollisionevent_t *pEvent );
	virtual void	VPhysicsShadowUpdate( IPhysicsObject *pPhysics ) {}
	virtual void	VPhysicsCollision( int index, gamevcollisionevent_t *pEvent );
	virtual void	VPhysicsFriction( IPhysicsObject *pObject, float energy, int surfaceProps, int surfacePropsHit );
	
	// update the shadow so it will coincide with the current AI position at some time
	// in the future (or 0 for now)
	virtual void	UpdatePhysicsShadowToCurrentPosition( float deltaTime );
	virtual int		VPhysicsGetObjectList( IPhysicsObject **pList, int listMax );
	virtual bool	VPhysicsIsFlesh( void );
	// --------------------------------------------------------------------
		// Is the entity floating?
	bool					IsFloating();
	void SetScaledPhysics(IPhysicsObject* pNewObject);
public:
//#if !defined( NO_ENTITY_PREDICTION )
//	// The player drives simulation of this entity
//	void					SetPlayerSimulated( CBasePlayer *pOwner );
//	void					UnsetPlayerSimulated( void );
//	bool					IsPlayerSimulated( void ) const;
//	CBasePlayer				*GetSimulatingPlayer( void );
//#endif
	// FIXME: Make these private!

	bool					PhysicsTestEntityPosition( CBaseEntity **ppEntity = NULL );
	void					PhysicsPushEntity( const Vector& push, trace_t *pTrace );
	bool					PhysicsCheckWater( void );
	void					PhysicsCheckWaterTransition( void );
	void					PhysicsStepRecheckGround();
	// Computes the water level + type
	void					UpdateWaterState();
	//bool					IsEdictFree() const { return edict()->IsFree(); }

	// Callbacks for the physgun/cannon picking up an entity
	virtual	CBasePlayer		*HasPhysicsAttacker( float dt ) { return NULL; }

	// UNDONE: Make this data?
	virtual unsigned int	PhysicsSolidMaskForEntity( void ) const;

	// Computes the abs position of a point specified in local space
	//void					ComputeAbsPosition( const Vector &vecLocalPosition, Vector *pAbsPosition );

	// Computes the abs position of a direction specified in local space
	//void					ComputeAbsDirection( const Vector &vecLocalDirection, Vector *pAbsDirection );

	//void					SetPredictionEligible( bool canpredict );

public:
	void					OnPositionChenged();
	void					OnAnglesChanged();
	void					OnAnimationChanged();
	void					AddWatcherToEntity(CBaseEntity* pWatcher, int watcherType);
	void					RemoveWatcherFromEntity(CBaseEntity* pWatcher, int watcherType);
	void					NotifyPositionChanged();
	void					NotifyVPhysicsStateChanged(IPhysicsObject* pPhysics, bool bAwake);
protected:

	int						PhysicsClipVelocity (const Vector& in, const Vector& normal, Vector& out, float overbounce );
	void					PhysicsRelinkChildren( float dt );

	// Performs the collision resolution for fliers.
	void					PerformFlyCollisionResolution( trace_t &trace, Vector &move );
	void					ResolveFlyCollisionBounce( trace_t &trace, Vector &vecVelocity, float flMinTotalElasticity = 0.0f );
	void					ResolveFlyCollisionSlide( trace_t &trace, Vector &vecVelocity );
	virtual void			ResolveFlyCollisionCustom( trace_t &trace, Vector &vecVelocity );

private:
	// Physics-related private methods
	void					PhysicsStep( void );
	void					PhysicsPusher( void );
	void					PhysicsNone( void );
	void					PhysicsNoclip( void );
	void					PhysicsStepRunTimestep( float timestep );
	void					PhysicsToss( void );
	void					PhysicsCustom( void );
	void					PerformPush( float movetime );

	// Simulation in local space of rigid children
	void					PhysicsRigidChild( void );

	// Computes the base velocity
	void					UpdateBaseVelocity( void );

	// Implement this if you use MOVETYPE_CUSTOM
	virtual void			PerformCustomPhysics( Vector *pNewPosition, Vector *pNewVelocity, QAngle *pNewAngles, QAngle *pNewAngVelocity );


	CBaseEntity				*PhysicsPushMove( float movetime );
	CBaseEntity				*PhysicsPushRotate( float movetime );

	CBaseEntity				*PhysicsCheckRotateMove( rotatingpushmove_t &rotPushmove, CBaseEntity **pPusherList, int pusherListCount );
	CBaseEntity				*PhysicsCheckPushMove( const Vector& move, CBaseEntity **pPusherList, int pusherListCount );
	int						PhysicsTryMove( float flTime, trace_t *steptrace );

	void					PhysicsCheckVelocity( void );
	void					PhysicsAddHalfGravity( float timestep );
	void					PhysicsAddGravityMove( Vector &move );

	void					CalcAbsoluteAngularVelocity();

	// Checks a sweep without actually performing the move
	void					PhysicsCheckSweep( const Vector& vecAbsStart, const Vector &vecAbsDelta, trace_t *pTrace );

	// Computes new angles based on the angular velocity
	void					SimulateAngles( float flFrameTime );

	// Run regular think and latch off angle/origin changes so we can interpolate them on the server to fake simulation
	void					StepSimulationThink( float dt );

public:
	// Add a discontinuity to a step
	bool					AddStepDiscontinuity( float flTime, const Vector &vecOrigin, const QAngle &vecAngles );
	// origin and angles to use in step calculations
	virtual	Vector			GetStepOrigin(void) const;
	virtual	QAngle			GetStepAngles(void) const;
private:

	


	friend class CPushBlockerEnum;

	

	// Shot statistics
	void UpdateShotStatistics( const trace_t &tr );

	// Handle shot entering water
	bool HandleShotImpactingWater( const FireBulletsInfo_t &info, const Vector &vecEnd, ITraceFilter *pTraceFilter, Vector *pVecTracerDest );

	// Handle shot entering water
	void HandleShotImpactingGlass( const FireBulletsInfo_t &info, const trace_t &tr, const Vector &vecDir, ITraceFilter *pTraceFilter );

	// Should we draw bubbles underwater?
	bool ShouldDrawUnderwaterBulletBubbles();

	// Computes the tracer start position
	void ComputeTracerStartPosition( const Vector &vecShotSrc, Vector *pVecTracerStart );

	// Computes the tracer start position
	void CreateBubbleTrailTracer( const Vector &vecShotSrc, const Vector &vecShotEnd, const Vector &vecShotDir );

	virtual bool ShouldDrawWaterImpacts() { return true; }

	// Changes shadow cast distance over time
	void ShadowCastDistThink( );

public:
	// Precache model sounds + particles
	static void PrecacheModelComponents( int nModelIndex );
	static void PrecacheSoundHelper( const char *pName );

	void InitStepHeightAdjust(void);
	void SetIKGroundContactInfo(float minHeight, float maxHeight);
	void UpdateStepOrigin(void);

	virtual void SetLightingOriginRelative(CBaseEntity* pLightingOriginRelative);
	void SetLightingOriginRelative(string_t strLightingOriginRelative);
	CBaseEntity* GetLightingOriginRelative();

	void SetLightingOrigin(CBaseEntity* pLightingOrigin);
	void SetLightingOrigin(string_t strLightingOrigin);
	CBaseEntity* GetLightingOrigin();

protected:
	// Which frame did I simulate?
	int						m_nSimulationTick;

private:


	// Damage modifiers
	friend class CDamageModifier;
	CUtlLinkedList<CDamageModifier*,int>	m_DamageModifiers;

	//EHANDLE m_pParent;  // for movement hierarchy
	byte	m_nTransmitStateOwnedCounter;


	friend class CServerNetworkProperty;

	CNetworkHandle( CBaseEntity, m_hOwnerEntity );	// only used to point to an edict it won't collide with
	CNetworkHandle( CBaseEntity, m_hEffectEntity );	// Fire/Dissolve entity.

	IPhysicsObject	*m_pPhysicsObject;	// pointer to the entity's physics object (vphysics.dll)

	CNetworkVar( float, m_flShadowCastDistance );
	float		m_flDesiredShadowCastDistance;

	// Team handling
	int			m_iInitialTeamNum;		// Team number of this entity's team read from file
	CNetworkVar( int, m_iTeamNum );				// Team number of this entity's team. 

	// Sets water type + level for physics objects
	unsigned char	m_nWaterTouch;
	unsigned char	m_nSlimeTouch;
	unsigned char	m_nWaterType;
	CNetworkVarForDerived( unsigned char, m_nWaterLevel );
	float			m_flNavIgnoreUntilTime;

	// Velocity of the thing we're standing on (world space)
	CNetworkVarForDerived( Vector, m_vecBaseVelocity );

	

	// Local angular velocity
	QAngle			m_vecAngVelocity;

	// Global angular velocity
//	QAngle			m_vecAbsAngVelocity;

	

	// Physics state
	EHANDLE			m_pBlocker;

	// was pev->ltime
	float			m_flLocalTime;
	// local time at the beginning of this frame
	float			m_flVPhysicsUpdateLocalTime;
	// local time the movement has ended
	float			m_flMoveDoneTime;

	// A counter to help quickly build a list of potentially pushed objects for physics
	int				m_nPushEnumCount;


	float				m_flIKGroundContactTime;
	float				m_flIKGroundMinHeight;
	float				m_flIKGroundMaxHeight;

	float				m_flEstIkFloor; // debounced
	float				m_flEstIkOffset;

	//Adrian
	CNetworkVar( unsigned char, m_iTextureFrameIndex );
	
	CNetworkVar( bool, m_bAlternateSorting );

	// User outputs. Fired when the "FireInputX" input is triggered.
	COutputEvent m_OnUser1;
	COutputEvent m_OnUser2;
	COutputEvent m_OnUser3;
	COutputEvent m_OnUser4;

	float				m_flDissolveStartTime;
	COutputEvent m_OnIgnite;

	CNetworkHandle(CBaseEntity, m_hLightingOrigin);
	CNetworkHandle(CBaseEntity, m_hLightingOriginRelative);

	string_t m_iszLightingOriginRelative;	// for reading from the file only
	string_t m_iszLightingOrigin;			// for reading from the file only
	
	CNetworkVar(float, m_fadeMinDist);	// Point at which fading is absolute
	CNetworkVar(float, m_fadeMaxDist);	// Point at which fading is inactive
	CNetworkVar(float, m_flFadeScale);	// Scale applied to min / max
	//CBaseHandle m_RefEHandle;

	// was pev->view_ofs ( FIXME:  Move somewhere up the hierarch, CBaseAnimating, etc. )
	CNetworkVectorForDerived( m_vecViewOffset );

//private:
	// dynamic model state tracking
	//bool m_bDynamicModelAllowed;
	//bool m_bDynamicModelPending;
	//bool m_bDynamicModelSetBounds;
	//void OnModelLoadComplete( const model_t* model );
	//friend class CBaseEntityModelLoadProxy;

//protected:
	//void EnableDynamicModels() { m_bDynamicModelAllowed = true; }

public:
	//bool IsDynamicModelLoading() const { return m_bDynamicModelPending; } 
	void SetCollisionBoundsFromModel();
	virtual	void RefreshCollisionBounds(void);

//#if !defined( NO_ENTITY_PREDICTION )
//	CNetworkVar( bool, m_bIsPlayerSimulated );
//	// Player who is driving my simulation
//	CHandle< CBasePlayer >			m_hPlayerSimulationOwner;
//#endif


	// So it can get at the physics methods
	friend class CCollisionEvent;

// Methods shared by client and server
public:
	void							SetSize( const Vector &vecMin, const Vector &vecMax ); // UTIL_SetSize( this, mins, maxs );
	//static int						PrecacheModel( const char *name, bool bPreload = true ); 
	//static bool						PrecacheSound( const char *name );
	//static void						PrefetchSound( const char *name );
	void							Remove( ); // UTIL_Remove( this );

private:

	// This is a random seed used by the networking code to allow client - side prediction code
	//  randon number generators to spit out the same random numbers on both sides for a particular
	//  usercmd input.
	static int						m_nPredictionRandomSeed;
	static CBasePlayer				*m_pPredictionPlayer;

	
	
public:
	// Accessors for above
	static int						GetPredictionRandomSeed( void );
	static void						SetPredictionRandomSeed( const CUserCmd *cmd );
	static CBasePlayer				*GetPredictionPlayer( void );
	static void						SetPredictionPlayer( CBasePlayer *player );


	// For debugging shared code
	static bool						IsServer( void )
	{
		return true;
	}

	static bool						IsClient( void )
	{
		return false;
	}

	static char const				*GetDLLType( void )
	{
		return "server";
	}
	
	// Used to access m_vecAbsOrigin during restore when it's unsafe to call GetAbsOrigin.
	friend class CPlayerRestoreHelper;
	
	static bool s_bAbsQueriesValid;

	// Call this when hierarchy is not completely set up (such as during Restore) to throw asserts
	// when people call GetAbsAnything. 
	static inline void SetAbsQueriesValid( bool bValid )
	{
		s_bAbsQueriesValid = bValid;
	}
	
	static inline bool IsAbsQueriesValid()
	{
		return s_bAbsQueriesValid;
	}

};

// Send tables exposed in this module.
EXTERN_SEND_TABLE(DT_BaseEntity);



// Ugly technique to override base member functions
// Normally it's illegal to cast a pointer to a member function of a derived class to a pointer to a 
// member function of a base class.  static_cast is a sleezy way around that problem.

#ifdef _DEBUG

#define SetTouch( a ) TouchSet( static_cast <void (CBaseEntity::*)(CBaseEntity *)> (a), #a )
#define SetUse( a ) UseSet( static_cast <void (CBaseEntity::*)(	CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )> (a), #a )
#define SetBlocked( a ) BlockedSet( static_cast <void (CBaseEntity::*)(CBaseEntity *)> (a), #a )

#else

#define SetTouch( a ) m_pfnTouch = static_cast <void (CBaseEntity::*)(CBaseEntity *)> (a)
#define SetUse( a ) m_pfnUse = static_cast <void (CBaseEntity::*)( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )> (a)
#define SetBlocked( a ) m_pfnBlocked = static_cast <void (CBaseEntity::*)(CBaseEntity *)> (a)

#endif

// handling entity/edict transforms
//inline CBaseEntity *GetContainingEntity( edict_t *pent )
//{
//	if ( pent && pent->GetUnknown() )
//	{
//		return pent->GetUnknown()->GetBaseEntity();
//	}
//
//	return NULL;
//}

//-----------------------------------------------------------------------------
// Purpose: Pauses or resumes entity i/o events. When paused, no outputs will
//			fire unless Debug_SetSteps is called with a nonzero step value.
// Input  : bPause - true to pause, false to resume.
//-----------------------------------------------------------------------------
inline void CBaseEntity::Debug_Pause(bool bPause)
{
	CBaseEntity::m_bDebugPause = bPause;
}


//-----------------------------------------------------------------------------
// Purpose: Returns true if entity i/o is paused, false if not.
//-----------------------------------------------------------------------------
inline bool CBaseEntity::Debug_IsPaused(void)
{
	return(CBaseEntity::m_bDebugPause);
}


//-----------------------------------------------------------------------------
// Purpose: Decrements the debug step counter. Used when the entity i/o system
//			is in single step mode, this is called every time an output is fired.
// Output : Returns true on to continue firing outputs, false to stop.
//-----------------------------------------------------------------------------
inline bool CBaseEntity::Debug_Step(void)
{
	if (CBaseEntity::m_nDebugSteps > 0)
	{
		CBaseEntity::m_nDebugSteps--;
	}
	return(CBaseEntity::m_nDebugSteps > 0);
}


//-----------------------------------------------------------------------------
// Purpose: Sets the number of entity outputs to allow to fire before pausing
//			the entity i/o system.
// Input  : nSteps - Number of steps to execute.
//-----------------------------------------------------------------------------
inline void CBaseEntity::Debug_SetSteps(int nSteps)
{
	CBaseEntity::m_nDebugSteps = nSteps;
}


//-----------------------------------------------------------------------------
// Purpose: Returns true if we should allow outputs to be fired, false if not.
//-----------------------------------------------------------------------------
inline bool CBaseEntity::Debug_ShouldStep(void)
{
	return(!CBaseEntity::m_bDebugPause || CBaseEntity::m_nDebugSteps > 0);
}

//-----------------------------------------------------------------------------
// Methods relating to traversing hierarchy
//-----------------------------------------------------------------------------
//inline CBaseEntity *CBaseEntity::GetMoveParent( void )
//{
//	return GetEngineObject()->GetMoveParent()? GetEngineObject()->GetMoveParent()->GetOuter():NULL;
//}

//inline CBaseEntity *CBaseEntity::FirstMoveChild( void )
//{
//	return GetEngineObject()->FirstMoveChild()? GetEngineObject()->FirstMoveChild()->GetOuter():NULL;
//}

//inline CBaseEntity *CBaseEntity::NextMovePeer( void )
//{
//	return GetEngineObject()->NextMovePeer()? GetEngineObject()->NextMovePeer()->GetOuter():NULL;
//}

//-----------------------------------------------------------------------------
// Returns the highest parent of an entity
//-----------------------------------------------------------------------------
//inline CBaseEntity* CBaseEntity::GetRootMoveParent()
//{
//	return GetEngineObject()->GetRootMoveParent() ? GetEngineObject()->GetRootMoveParent()->GetOuter() : NULL;
//}

// FIXME: Remove this! There shouldn't be a difference between moveparent + parent
//inline CBaseEntity* CBaseEntity::GetParent()
//{
//	return m_hMoveParent.Get();
//}

inline void	CBaseEntity::SetNavIgnore( float duration )
{
	float flNavIgnoreUntilTime = ( duration == FLT_MAX ) ? FLT_MAX : gpGlobals->curtime + duration;
	if ( flNavIgnoreUntilTime > m_flNavIgnoreUntilTime )
		m_flNavIgnoreUntilTime = flNavIgnoreUntilTime;
}

inline void	CBaseEntity::ClearNavIgnore()
{
	m_flNavIgnoreUntilTime = 0;
}

inline bool	CBaseEntity::IsNavIgnored() const
{
	return ( gpGlobals->curtime <= m_flNavIgnoreUntilTime );
}



//-----------------------------------------------------------------------------
// Network state optimization
//-----------------------------------------------------------------------------
inline CBaseCombatCharacter *ToBaseCombatCharacter( CBaseEntity *pEntity )
{
	if ( !pEntity )
		return NULL;

	return pEntity->MyCombatCharacterPointer();
}


//-----------------------------------------------------------------------------
// Velocity
//-----------------------------------------------------------------------------
inline Vector CBaseEntity::GetSmoothedVelocity( void )
{
	Vector vel;
	GetVelocity( &vel, NULL );
	return vel;
}

inline const QAngle &CBaseEntity::GetLocalAngularVelocity( ) const
{
	return m_vecAngVelocity;
}

/*
// FIXME: While we're using (dPitch, dYaw, dRoll) as our local angular velocity
// representation, we can't actually solve this problem
inline const QAngle &CBaseEntity::GetAbsAngularVelocity( ) const
{
	if (IsEFlagSet(EFL_DIRTY_ABSANGVELOCITY))
	{
		const_cast<CBaseEntity*>(this)->CalcAbsoluteAngularVelocity();
	}

	return m_vecAbsAngVelocity;
}
*/

inline const Vector& CBaseEntity::GetBaseVelocity() const 
{ 
	return m_vecBaseVelocity.Get(); 
}

inline void CBaseEntity::SetBaseVelocity( const Vector& v ) 
{ 
	m_vecBaseVelocity = v; 
}


inline void	CBaseEntity::SetShadowCastDistance( float flDistance )
{ 
	m_flShadowCastDistance = flDistance; 
}

inline float CBaseEntity::GetShadowCastDistance( void )	const			
{ 
	return m_flShadowCastDistance; 
}

inline float CBaseEntity::GetLocalTime( void ) const
{ 
	return m_flLocalTime; 
}

inline void CBaseEntity::IncrementLocalTime( float flTimeDelta )
{ 
	m_flLocalTime += flTimeDelta; 
}

inline float CBaseEntity::GetMoveDoneTime( ) const
{
	return (m_flMoveDoneTime >= 0) ? m_flMoveDoneTime - GetLocalTime() : -1;
}

inline int CBaseEntity::GetWaterLevel() const
{
	return m_nWaterLevel;
}

inline void CBaseEntity::SetWaterLevel( int nLevel )
{
	m_nWaterLevel = nLevel;
}

inline const color32 CBaseEntity::GetRenderColor() const
{
	return m_clrRender.Get();
}

inline void CBaseEntity::SetRenderColor( byte r, byte g, byte b )
{
	m_clrRender.Init( r, g, b );
}

inline void CBaseEntity::SetRenderColor( byte r, byte g, byte b, byte a )
{
	m_clrRender.Init( r, g, b, a );
}

inline void CBaseEntity::SetRenderColorR( byte r )
{
	m_clrRender.SetR( r );
}

inline void CBaseEntity::SetRenderColorG( byte g )
{
	m_clrRender.SetG( g );
}

inline void CBaseEntity::SetRenderColorB( byte b )
{
	m_clrRender.SetB( b );
}

inline void CBaseEntity::SetRenderColorA( byte a )
{
	m_clrRender.SetA( a );
}

inline int	CBaseEntity::GetTextureFrameIndex( void )
{
	return m_iTextureFrameIndex;
}

inline void CBaseEntity::SetTextureFrameIndex( int iIndex )
{
	m_iTextureFrameIndex = iIndex;
}

inline CEntityNetworkProperty *CBaseEntity::NetworkProp()
{
	return &m_Network;
}

inline const CEntityNetworkProperty *CBaseEntity::NetworkProp() const
{
	return &m_Network;
}
 	 			 
//-----------------------------------------------------------------------------
// Methods related to IServerUnknown
//-----------------------------------------------------------------------------
inline ICollideable *CBaseEntity::GetCollideable()
{
	return GetEngineObject()->GetCollideable();
}

inline IServerNetworkable *CBaseEntity::GetNetworkable()
{
	return &m_Network;
}

inline CBaseEntity *CBaseEntity::GetBaseEntity()
{
	return this;
}

//-----------------------------------------------------------------------------
// Methods relating to bounds
//-----------------------------------------------------------------------------
inline const Vector& CBaseEntity::WorldAlignMins( ) const
{
	Assert( !GetEngineObject()->IsBoundsDefinedInEntitySpace() );
	Assert(GetEngineObject()->GetCollisionAngles() == vec3_angle );
	return GetEngineObject()->OBBMins();
}

inline const Vector& CBaseEntity::WorldAlignMaxs( ) const
{
	Assert( !GetEngineObject()->IsBoundsDefinedInEntitySpace() );
	Assert(GetEngineObject()->GetCollisionAngles() == vec3_angle );
	return GetEngineObject()->OBBMaxs();
}

inline const Vector& CBaseEntity::WorldAlignSize( ) const
{
	Assert( !GetEngineObject()->IsBoundsDefinedInEntitySpace() );
	Assert(GetEngineObject()->GetCollisionAngles() == vec3_angle );
	return GetEngineObject()->OBBSize();
}

// Returns a radius of a sphere *centered at the world space center*
// bounding the collision representation of the entity
//inline float CBaseEntity::BoundingRadius() const
//{
//	return GetEngineObject()->BoundingRadius();
//}

inline bool CBaseEntity::IsTransparent() const
{
	return m_nRenderMode != kRenderNormal;
}

inline void CBaseEntity::SetRenderMode( RenderMode_t nRenderMode )
{
	m_nRenderMode = nRenderMode;
}

inline RenderMode_t CBaseEntity::GetRenderMode() const
{
	return (RenderMode_t)m_nRenderMode.Get();
}


//-----------------------------------------------------------------------------
// Methods to cast away const
//-----------------------------------------------------------------------------
inline Vector CBaseEntity::EyePosition( void ) const
{
	return const_cast<CBaseEntity*>(this)->EyePosition();
}

inline const QAngle &CBaseEntity::EyeAngles( void ) const		// Direction of eyes in world space
{
	return const_cast<CBaseEntity*>(this)->EyeAngles();
}

inline const QAngle &CBaseEntity::LocalEyeAngles( void ) const	// Direction of eyes
{
	return const_cast<CBaseEntity*>(this)->LocalEyeAngles();
}

inline Vector	CBaseEntity::EarPosition( void ) const			// position of ears
{
	return const_cast<CBaseEntity*>(this)->EarPosition();
}


//-----------------------------------------------------------------------------
// Methods relating to networking
//-----------------------------------------------------------------------------
inline void	CBaseEntity::NetworkStateChanged()
{
	NetworkProp()->NetworkStateChanged();
}


inline void	CBaseEntity::NetworkStateChanged( void *pVar )
{
	// Make sure it's a semi-reasonable pointer.
	Assert( (char*)pVar > (char*)this );
	Assert( (char*)pVar - (char*)this < 32768 );
	
	// Good, they passed an offset so we can track this variable's change
	// and avoid sending the whole entity.
	NetworkProp()->NetworkStateChanged( (char*)pVar - (char*)this );
}

inline void	CBaseEntity::NetworkStateChanged(unsigned short varOffset)
{
	// Make sure it's a semi-reasonable pointer.
	//Assert((char*)pVar > (char*)this);
	//Assert((char*)pVar - (char*)this < 32768);

	// Good, they passed an offset so we can track this variable's change
	// and avoid sending the whole entity.
	NetworkProp()->NetworkStateChanged(varOffset);
}


//-----------------------------------------------------------------------------
// IHandleEntity overrides.
//-----------------------------------------------------------------------------
//inline const CBaseHandle& CBaseEntity::GetRefEHandle() const
//{
//	return m_RefEHandle;
//}

inline void CBaseEntity::IncrementTransmitStateOwnedCounter()
{
	Assert( m_nTransmitStateOwnedCounter != 255 );
	m_nTransmitStateOwnedCounter++;
}

inline void CBaseEntity::DecrementTransmitStateOwnedCounter()
{
	Assert( m_nTransmitStateOwnedCounter != 0 );
	m_nTransmitStateOwnedCounter--;
}

inline void CBaseEntity::SetLightingOrigin(CBaseEntity* pLightingOrigin)
{
	m_hLightingOrigin = pLightingOrigin;
}

inline CBaseEntity* CBaseEntity::GetLightingOrigin()
{
	return m_hLightingOrigin;
}

inline void CBaseEntity::SetLightingOriginRelative(CBaseEntity* pLightingOriginRelative)
{
	m_hLightingOriginRelative = pLightingOriginRelative;
}

inline CBaseEntity* CBaseEntity::GetLightingOriginRelative()
{
	return m_hLightingOriginRelative;
}

//-----------------------------------------------------------------------------
// Bullet firing (legacy)...
//-----------------------------------------------------------------------------
inline void CBaseEntity::FireBullets( int cShots, const Vector &vecSrc, 
	const Vector &vecDirShooting, const Vector &vecSpread, float flDistance, 
	int iAmmoType, int iTracerFreq, int firingEntID, int attachmentID,
	int iDamage, CBaseEntity *pAttacker, bool bFirstShotAccurate, bool bPrimaryAttack )
{
	FireBulletsInfo_t info;
	info.m_iShots = cShots;
	info.m_vecSrc = vecSrc;
	info.m_vecDirShooting = vecDirShooting;
	info.m_vecSpread = vecSpread;
	info.m_flDistance = flDistance;
	info.m_iAmmoType = iAmmoType;
	info.m_iTracerFreq = iTracerFreq;
	info.m_flDamage = iDamage;
	info.m_pAttacker = pAttacker;
	info.m_nFlags = bFirstShotAccurate ? FIRE_BULLETS_FIRST_SHOT_ACCURATE : 0;
	info.m_bPrimaryAttack = bPrimaryAttack;

	FireBullets( info );
}

// Ugly technique to override base member functions
// Normally it's illegal to cast a pointer to a member function of a derived class to a pointer to a 
// member function of a base class.  static_cast is a sleezy way around that problem.

#define SetThink( a ) GetEngineObject()->ThinkSet( static_cast <void (CBaseEntity::*)(void)> (a), 0, NULL )
#define SetContextThink( a, b, context ) GetEngineObject()->ThinkSet( static_cast <void (CBaseEntity::*)(void)> (a), (b), context )

#ifdef _DEBUG
#define SetMoveDone( a ) \
	do \
	{ \
		m_pfnMoveDone = static_cast <void (CBaseEntity::*)(void)> (a); \
		FunctionCheck( (void *)*((int *)((char *)this + ( offsetof(CBaseEntity,m_pfnMoveDone)))), "BaseMoveFunc" ); \
	} while ( 0 )
#else
#define SetMoveDone( a ) \
		(void)(m_pfnMoveDone = static_cast <void (CBaseEntity::*)(void)> (a))
#endif


inline bool FClassnameIs(CBaseEntity *pEntity, const char *szClassname)
{ 
	return pEntity->ClassMatches(szClassname); 
}

class CPointEntity : public CBaseEntity
{
public:
	DECLARE_CLASS( CPointEntity, CBaseEntity );

	void	Spawn( void );
	virtual int	ObjectCaps( void ) { return BaseClass::ObjectCaps() & ~FCAP_ACROSS_TRANSITION; }
	virtual bool KeyValue( const char *szKeyName, const char *szValue );
private:
};

// Has a position + size
class CServerOnlyEntity : public CBaseEntity
{
	DECLARE_CLASS( CServerOnlyEntity, CBaseEntity );
public:
	CServerOnlyEntity() : CBaseEntity() {}
	
	static bool IsNetworkableStatic(void) { return false; }
	virtual bool IsNetworkable(void) { return CServerOnlyEntity::IsNetworkableStatic(); }
	virtual int ObjectCaps( void ) { return (BaseClass::ObjectCaps() & ~FCAP_ACROSS_TRANSITION); }
};

// Has only a position, no size
class CServerOnlyPointEntity : public CServerOnlyEntity
{
	DECLARE_CLASS( CServerOnlyPointEntity, CServerOnlyEntity );

public:
	virtual bool KeyValue( const char *szKeyName, const char *szValue );
};

// Has no position or size
class CLogicalEntity : public CServerOnlyEntity
{
	DECLARE_CLASS( CLogicalEntity, CServerOnlyEntity );

public:
	virtual bool KeyValue( const char *szKeyName, const char *szValue );
};


// Network proxy functions

//void SendProxy_Origin( const SendProp *pProp, const void *pStruct, const void *pData, DVariant *pOut, int iElement, int objectID );
//void SendProxy_OriginXY( const SendProp *pProp, const void *pStruct, const void *pData, DVariant *pOut, int iElement, int objectID );
//void SendProxy_OriginZ( const SendProp *pProp, const void *pStruct, const void *pData, DVariant *pOut, int iElement, int objectID );
//void SendProxy_Angles(const SendProp* pProp, const void* pStruct, const void* pData, DVariant* pOut, int iElement, int objectID);

#endif // BASEENTITY_H
