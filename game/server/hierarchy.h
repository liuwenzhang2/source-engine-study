//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Contains the set of functions for manipulating entity hierarchies.
//
// $NoKeywords: $
//=============================================================================//

#ifndef HIERARCHY_H
#define HIERARCHY_H
#ifdef _WIN32
#pragma once
#endif

#include "utlvector.h"

class CBaseEntity;

//void		UnlinkFromParent( CEngineObject *pRemove );
//void		TransferChildren( CEngineObject *pOldParent, CEngineObject *pNewParent );
//void		LinkChild( CEngineObject *pParent, CEngineObject *pChild );
//void		UnlinkAllChildren( CEngineObject *pParent );
int			GetAllChildren(IEngineObjectServer*pParent, CUtlVector<IEngineObjectServer*> &list );
bool		EntityIsParentOf( IEngineObjectServer *pParent, IEngineObjectServer*pEntity );
int			GetAllInHierarchy(IEngineObjectServer*pParent, CUtlVector<IEngineObjectServer*> &list );

#endif // HIERARCHY_H
