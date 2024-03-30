//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef EHANDLE_H
#define EHANDLE_H
#ifdef _WIN32
#pragma once
#endif

#if defined( _DEBUG ) && defined( GAME_DLL )
#include "tier0/dbg.h"
#include "cbase.h"
#endif


#include "const.h"
#include "basehandle.h"
#include "networkvar.h"
//#include "entitylist_base.h"


//class IHandleEntity;
#define INVALID_ENTITY_HANDLE INVALID_EHANDLE_INDEX







#endif // EHANDLE_H
