//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef DATAMAP_H
#define DATAMAP_H
#ifdef _WIN32
#pragma once
#endif

#ifndef VECTOR_H
#include "mathlib/vector.h"
#endif

#include "tier1/utlvector.h"
#include "ihandleentity.h"
#include "string_t.h"
#include "tier0/memdbgon.h"

struct inputdata_t;

#define INVALID_TIME (FLT_MAX * -1.0) // Special value not rebased on save/load

typedef enum _fieldtypes
{
	FIELD_VOID = 0,			// No type or value
	FIELD_FLOAT,			// Any floating point value
	FIELD_STRING,			// A string ID (return from ALLOC_STRING)
	FIELD_VECTOR,			// Any vector, QAngle, or AngularImpulse
	FIELD_QUATERNION,		// A quaternion
	FIELD_INTEGER,			// Any integer or enum
	FIELD_BOOLEAN,			// boolean, implemented as an int, I may use this as a hint for compression
	FIELD_SHORT,			// 2 byte integer
	FIELD_CHARACTER,		// a byte
	FIELD_COLOR32,			// 8-bit per channel r,g,b,a (32bit color)
	FIELD_EMBEDDED,			// an embedded object with a datadesc, recursively traverse and embedded class/structure based on an additional typedescription
	FIELD_CUSTOM,			// special type that contains function pointers to it's read/write/parse functions

	FIELD_CLASSPTR,			// CBaseEntity *
	FIELD_EHANDLE,			// Entity handle
	FIELD_EDICT,			// edict_t *

	FIELD_POSITION_VECTOR,	// A world coordinate (these are fixed up across level transitions automagically)
	FIELD_TIME,				// a floating point time (these are fixed up automatically too!)
	FIELD_TICK,				// an integer tick count( fixed up similarly to time)
	FIELD_MODELNAME,		// Engine string that is a model name (needs precache)
	FIELD_SOUNDNAME,		// Engine string that is a sound name (needs precache)

	FIELD_INPUT,			// a list of inputed data fields (all derived from CMultiInputVar)
	FIELD_FUNCTION,			// A class function pointer (Think, Use, etc)

	FIELD_VMATRIX,			// a vmatrix (output coords are NOT worldspace)

	// NOTE: Use float arrays for local transformations that don't need to be fixed up.
	FIELD_VMATRIX_WORLDSPACE,// A VMatrix that maps some local space to world space (translation is fixed up on level transitions)
	FIELD_MATRIX3X4_WORLDSPACE,	// matrix3x4_t that maps some local space to world space (translation is fixed up on level transitions)

	FIELD_INTERVAL,			// a start and range floating point interval ( e.g., 3.2->3.6 == 3.2 and 0.4 )
	FIELD_MODELINDEX,		// a model index
	FIELD_MATERIALINDEX,	// a material index (using the material precache string table)

	FIELD_VECTOR2D,			// 2 floats
	FIELD_INTEGER64,        // 64bit integer
	FIELD_POINTER,

	FIELD_TYPECOUNT,		// MUST BE LAST
} fieldtype_t;


//-----------------------------------------------------------------------------
// Field sizes... 
//-----------------------------------------------------------------------------
template <int FIELD_TYPE>
class CDatamapFieldSizeDeducer
{
public:
	enum
	{
		SIZE = 0
	};

	static int FieldSize( )
	{
		return 0;
	}
};

#define DECLARE_FIELD_SIZE( _fieldType, _fieldSize )	\
	template< > class CDatamapFieldSizeDeducer<_fieldType> { public: enum { SIZE = _fieldSize }; static int FieldSize() { return _fieldSize; } };
#define FIELD_SIZE( _fieldType )	CDatamapFieldSizeDeducer<_fieldType>::SIZE
#define FIELD_BITS( _fieldType )	(FIELD_SIZE( _fieldType ) * 8)

DECLARE_FIELD_SIZE( FIELD_FLOAT,		sizeof(float) )

DECLARE_FIELD_SIZE( FIELD_VECTOR,		3 * sizeof(float) )
DECLARE_FIELD_SIZE( FIELD_VECTOR2D,		2 * sizeof(float) )
DECLARE_FIELD_SIZE( FIELD_QUATERNION,	4 * sizeof(float))
DECLARE_FIELD_SIZE( FIELD_INTEGER,		sizeof(int))
DECLARE_FIELD_SIZE( FIELD_BOOLEAN,		sizeof(char))
DECLARE_FIELD_SIZE( FIELD_SHORT,		sizeof(short))
DECLARE_FIELD_SIZE( FIELD_CHARACTER,	sizeof(char))
DECLARE_FIELD_SIZE( FIELD_COLOR32,		sizeof(int))
DECLARE_FIELD_SIZE( FIELD_STRING,		sizeof(void*))
DECLARE_FIELD_SIZE( FIELD_POINTER,		sizeof(void*))
DECLARE_FIELD_SIZE( FIELD_MODELNAME,	sizeof(void*))
DECLARE_FIELD_SIZE( FIELD_SOUNDNAME,	sizeof(void*))
DECLARE_FIELD_SIZE( FIELD_EHANDLE,		sizeof(void*))
DECLARE_FIELD_SIZE( FIELD_CLASSPTR,		sizeof(void*))
DECLARE_FIELD_SIZE( FIELD_EDICT,		sizeof(void*))
DECLARE_FIELD_SIZE( FIELD_POSITION_VECTOR, 	3 * sizeof(float))
DECLARE_FIELD_SIZE( FIELD_TIME,			sizeof(float))
DECLARE_FIELD_SIZE( FIELD_TICK,			sizeof(int))
DECLARE_FIELD_SIZE( FIELD_INPUT,		sizeof(int))
#ifdef POSIX
// pointer to members under gnuc are 8bytes if you have a virtual func
DECLARE_FIELD_SIZE( FIELD_FUNCTION,		2 * sizeof(void *))
#else
DECLARE_FIELD_SIZE( FIELD_FUNCTION,		sizeof(void *))
#endif
DECLARE_FIELD_SIZE( FIELD_VMATRIX,		16 * sizeof(float))
DECLARE_FIELD_SIZE( FIELD_VMATRIX_WORLDSPACE,	16 * sizeof(float))
DECLARE_FIELD_SIZE( FIELD_MATRIX3X4_WORLDSPACE,	12 * sizeof(float))
DECLARE_FIELD_SIZE( FIELD_INTERVAL,		2 * sizeof( float) )  // NOTE:  Must match interval.h definition
DECLARE_FIELD_SIZE( FIELD_MODELINDEX,	sizeof(int) )
DECLARE_FIELD_SIZE( FIELD_MATERIALINDEX,	sizeof(int) )


#define ARRAYSIZE2D(p)		(sizeof(p)/sizeof(p[0][0]))
#define SIZE_OF_ARRAY(p)	_ARRAYSIZE(p)

#define _offsetof(s,m)	((int)(intp)&(((s *)0)->m))

#define _FIELD(name,fieldtype,count,flags,mapname,tolerance)		{ fieldtype, #name, { _offsetof(classNameTypedef, name), 0 }, count, flags, mapname, NULL, NULL, NULL, sizeof( ((classNameTypedef *)0)->name ), NULL, 0, tolerance }
#define DEFINE_FIELD_NULL	{ FIELD_VOID,0, {0,0},0,0,0,0,0,0}
#define DEFINE_FIELD(name,fieldtype)			_FIELD(name, fieldtype, 1,  FTYPEDESC_SAVE, NULL, 0 )
#define DEFINE_KEYFIELD(name,fieldtype, mapname) _FIELD(name, fieldtype, 1,  FTYPEDESC_KEY | FTYPEDESC_SAVE, mapname, 0 )
#define DEFINE_KEYFIELD_NOT_SAVED(name,fieldtype, mapname)_FIELD(name, fieldtype, 1,  FTYPEDESC_KEY, mapname, 0 )
#define DEFINE_AUTO_ARRAY(name,fieldtype)		_FIELD(name, fieldtype, SIZE_OF_ARRAY(((classNameTypedef *)0)->name), FTYPEDESC_SAVE, NULL, 0 )
#define DEFINE_AUTO_ARRAY_KEYFIELD(name,fieldtype,mapname)		_FIELD(name, fieldtype, SIZE_OF_ARRAY(((classNameTypedef *)0)->name), FTYPEDESC_SAVE, mapname, 0 )
#define DEFINE_ARRAY(name,fieldtype, count)		_FIELD(name, fieldtype, count, FTYPEDESC_SAVE, NULL, 0 )
//#define DEFINE_ENTITY_FIELD(name,fieldtype)			_FIELD(edict_t, name, fieldtype, 1,  FTYPEDESC_KEY | FTYPEDESC_SAVE, #name, 0 )
//#define DEFINE_ENTITY_GLOBAL_FIELD(name,fieldtype)	_FIELD(edict_t, name, fieldtype, 1,  FTYPEDESC_KEY | FTYPEDESC_SAVE | FTYPEDESC_GLOBAL, #name, 0 )
#define DEFINE_GLOBAL_FIELD(name,fieldtype)	_FIELD(name, fieldtype, 1,  FTYPEDESC_GLOBAL | FTYPEDESC_SAVE, NULL, 0 )
#define DEFINE_GLOBAL_KEYFIELD(name,fieldtype, mapname)	_FIELD(name, fieldtype, 1,  FTYPEDESC_GLOBAL | FTYPEDESC_KEY | FTYPEDESC_SAVE, mapname, 0 )
#define DEFINE_CUSTOM_FIELD(name,datafuncs)	{ FIELD_CUSTOM, #name, { _offsetof(classNameTypedef, name), 0 }, 1, FTYPEDESC_SAVE, NULL, datafuncs, NULL }
#define DEFINE_CUSTOM_KEYFIELD(name,datafuncs,mapname)	{ FIELD_CUSTOM, #name, { _offsetof(classNameTypedef, name), 0 }, 1, FTYPEDESC_SAVE | FTYPEDESC_KEY, mapname, datafuncs, NULL }
#define DEFINE_CUSTOM_FIELD_INVALID(name,datafuncs)	{ FIELD_CUSTOM, #name, { 0, 0 }, 1, FTYPEDESC_SAVE, NULL, datafuncs, NULL }
#define DEFINE_CUSTOM_KEYFIELD_INVALID(name,datafuncs,mapname)	{ FIELD_CUSTOM, #name, { 0, 0 }, 1, FTYPEDESC_SAVE | FTYPEDESC_KEY, mapname, datafuncs, NULL }
#define DEFINE_CUSTOM_GLOBAL_FIELD(name,datafuncs)	{ FIELD_CUSTOM, #name, { 0, 0 }, 1, FTYPEDESC_GLOBAL | FTYPEDESC_SAVE, NULL, datafuncs, NULL }
#define DEFINE_CUSTOM_GLOBAL_KEYFIELD_INVALID(name,datafuncs, mapname)	{ FIELD_CUSTOM, #name, { 0, 0 }, 1, FTYPEDESC_GLOBAL | FTYPEDESC_KEY | FTYPEDESC_SAVE, mapname, datafuncs, NULL }
#define DEFINE_AUTO_ARRAY2D(name,fieldtype)		_FIELD(name, fieldtype, ARRAYSIZE2D(((classNameTypedef *)0)->name), FTYPEDESC_SAVE, NULL, 0 )
// Used by byteswap datadescs
#define DEFINE_BITFIELD(name,fieldtype,bitcount)	DEFINE_ARRAY(name,fieldtype,((bitcount+FIELD_BITS(fieldtype)-1)&~(FIELD_BITS(fieldtype)-1)) / FIELD_BITS(fieldtype) )
#define DEFINE_INDEX(name,fieldtype)			_FIELD(name, fieldtype, 1,  FTYPEDESC_INDEX, NULL, 0 )

#define DEFINE_EMBEDDED( name )						\
	{ FIELD_EMBEDDED, #name, { _offsetof(classNameTypedef, name), 0 }, 1, FTYPEDESC_SAVE, NULL, NULL, NULL, &(((classNameTypedef *)0)->name.m_DataMap), sizeof( ((classNameTypedef *)0)->name ), NULL, 0, 0.0f }

#define DEFINE_EMBEDDED_OVERRIDE( name, overridetype )	\
	{ FIELD_EMBEDDED, #name, { _offsetof(classNameTypedef, name), 0 }, 1, FTYPEDESC_SAVE, NULL, NULL, NULL, &((overridetype *)0)->m_DataMap, sizeof( ((classNameTypedef *)0)->name ), NULL, 0, 0.0f }

#define DEFINE_EMBEDDEDBYREF( name )					\
	{ FIELD_EMBEDDED, #name, { _offsetof(classNameTypedef, name), 0 }, 1, FTYPEDESC_SAVE | FTYPEDESC_PTR, NULL, NULL, NULL, &(((classNameTypedef *)0)->name->m_DataMap), sizeof( *(((classNameTypedef *)0)->name) ), NULL, 0, 0.0f }

#define DEFINE_EMBEDDED_ARRAY( name, count )			\
	{ FIELD_EMBEDDED, #name, { _offsetof(classNameTypedef, name), 0 }, count, FTYPEDESC_SAVE, NULL, NULL, NULL, &(((classNameTypedef *)0)->name->m_DataMap), sizeof( ((classNameTypedef *)0)->name[0] ), NULL, 0, 0.0f  }

#define DEFINE_EMBEDDED_AUTO_ARRAY( name )			\
	{ FIELD_EMBEDDED, #name, { _offsetof(classNameTypedef, name), 0 }, SIZE_OF_ARRAY( ((classNameTypedef *)0)->name ), FTYPEDESC_SAVE, NULL, NULL, NULL, &(((classNameTypedef *)0)->name->m_DataMap), sizeof( ((classNameTypedef *)0)->name[0] ), NULL, 0, 0.0f  }

#ifndef NO_ENTITY_PREDICTION

#define DEFINE_PRED_TYPEDESCRIPTION( name, fieldtype )						\
	{ FIELD_EMBEDDED, #name, { _offsetof(classNameTypedef, name), 0 }, 1, FTYPEDESC_SAVE, NULL, NULL, NULL, &fieldtype::m_PredMap }

#define DEFINE_PRED_TYPEDESCRIPTION_PTR( name, fieldtype )						\
	{ FIELD_EMBEDDED, #name, { _offsetof(classNameTypedef, name), 0 }, 1, FTYPEDESC_SAVE | FTYPEDESC_PTR, NULL, NULL, NULL, &fieldtype::m_PredMap }

#else

#define DEFINE_PRED_TYPEDESCRIPTION( name, fieldtype )		DEFINE_FIELD_NULL
#define DEFINE_PRED_TYPEDESCRIPTION_PTR( name, fieldtype )	DEFINE_FIELD_NULL

#endif

// Extensions to datamap.h macros for predicted entities only
#define DEFINE_PRED_FIELD(name,fieldtype, flags)			_FIELD(name, fieldtype, 1,  flags, NULL, 0.0f )
#define DEFINE_PRED_ARRAY(name,fieldtype, count,flags)		_FIELD(name, fieldtype, count, flags, NULL, 0.0f )
#define DEFINE_FIELD_NAME(localname,netname,fieldtype)		_FIELD(localname, fieldtype, 1,  0, #netname, 0.0f )
// Predictable macros, which include a tolerance for floating point values...
#define DEFINE_PRED_FIELD_TOL(name,fieldtype, flags,tolerance)			_FIELD(name, fieldtype, 1,  flags, NULL, tolerance )
#define DEFINE_PRED_ARRAY_TOL(name,fieldtype, count,flags,tolerance)		_FIELD(name, fieldtype, count, flags, NULL, tolerance)
#define DEFINE_FIELD_NAME_TOL(localname,netname,fieldtolerance)		_FIELD(localname, fieldtype, 1,  0, #netname, tolerance )

//#define DEFINE_DATA( name, fieldextname, flags ) _FIELD(name, fieldtype, 1,  flags, extname )

// INPUTS
#define DEFINE_INPUT( name, fieldtype, inputname ) { fieldtype, #name, { _offsetof(classNameTypedef, name), 0 }, 1, FTYPEDESC_INPUT | FTYPEDESC_SAVE | FTYPEDESC_KEY,	inputname, NULL, NULL, NULL, sizeof( ((classNameTypedef *)0)->name ) }
#define DEFINE_INPUTFUNC( fieldtype, inputname, inputfunc ) { fieldtype, #inputfunc, { NULL, NULL }, 1, FTYPEDESC_INPUT, inputname, NULL, static_cast <inputfunc_t> (&classNameTypedef::inputfunc) }

// OUTPUTS
// the variable 'name' MUST BE derived from CBaseOutput
// we know the output type from the variable itself, so it doesn't need to be specified here
class ISaveRestoreOps;
extern ISaveRestoreOps *eventFuncs;
#define DEFINE_OUTPUT( name, outputname )	{ FIELD_CUSTOM, #name, { _offsetof(classNameTypedef, name), 0 }, 1, FTYPEDESC_OUTPUT | FTYPEDESC_SAVE | FTYPEDESC_KEY, outputname, eventFuncs }

// replaces EXPORT table for portability and non-DLL based systems (xbox)
#define DEFINE_FUNCTION_RAW( function, func_type )			{ FIELD_VOID, nameHolder.GenerateName(#function), { NULL, NULL }, 1, FTYPEDESC_FUNCTIONTABLE, NULL, NULL, (inputfunc_t)((func_type)(&classNameTypedef::function)) }
#define DEFINE_FUNCTION( function )			DEFINE_FUNCTION_RAW( function, inputfuncnoarg_t )


#define FTYPEDESC_GLOBAL			0x0001		// This field is masked for global entity save/restore
#define FTYPEDESC_SAVE				0x0002		// This field is saved to disk
#define FTYPEDESC_KEY				0x0004		// This field can be requested and written to by string name at load time
#define FTYPEDESC_INPUT				0x0008		// This field can be written to by string name at run time, and a function called
#define FTYPEDESC_OUTPUT			0x0010		// This field propogates it's value to all targets whenever it changes
#define FTYPEDESC_FUNCTIONTABLE		0x0020		// This is a table entry for a member function pointer
#define FTYPEDESC_PTR				0x0040		// This field is a pointer, not an embedded object
#define FTYPEDESC_OVERRIDE			0x0080		// The field is an override for one in a base class (only used by prediction system for now)

// Flags used by other systems (e.g., prediction system)
#define FTYPEDESC_INSENDTABLE		0x0100		// This field is present in a network SendTable
#define FTYPEDESC_PRIVATE			0x0200		// The field is local to the client or server only (not referenced by prediction code and not replicated by networking)
#define FTYPEDESC_NOERRORCHECK		0x0400		// The field is part of the prediction typedescription, but doesn't get compared when checking for errors

#define FTYPEDESC_MODELINDEX		0x0800		// The field is a model index (used for debugging output)

#define FTYPEDESC_INDEX				0x1000		// The field is an index into file data, used for byteswapping. 

// These flags apply to C_BasePlayer derived objects only
#define FTYPEDESC_VIEW_OTHER_PLAYER		0x2000		// By default you can only view fields on the local player (yourself), 
													//   but if this is set, then we allow you to see fields on other players
#define FTYPEDESC_VIEW_OWN_TEAM			0x4000		// Only show this data if the player is on the same team as the local player
#define FTYPEDESC_VIEW_NEVER			0x8000		// Never show this field to anyone, even the local player (unusual)

#define TD_MSECTOLERANCE		0.001f		// This is a FIELD_FLOAT and should only be checked to be within 0.001 of the networked info

class typedescription_t;


class ISaveRestoreOps;

//
// Function prototype for all input handlers.
//
typedef void (IHandleEntity::* inputfunc_t)(inputdata_t& data);
typedef void (IHandleEntity::* inputfuncnoarg_t)();

class datamap_t;
class typedescription_t;

enum
{
	TD_OFFSET_NORMAL = 0,
	TD_OFFSET_PACKED = 1,

	// Must be last
	TD_OFFSET_COUNT,
};

class typedescription_t
{
public:
	fieldtype_t			fieldType;
	const char			*fieldName;
	int					fieldOffset[ TD_OFFSET_COUNT ]; // 0 == normal, 1 == packed offset
	unsigned short		fieldSize;
	short				flags;
	// the name of the variable in the map/fgd data, or the name of the action
	const char			*externalName;	
	// pointer to the function set for save/restoring of custom data types
	ISaveRestoreOps		*pSaveRestoreOps; 
	// for associating function with string names
	inputfunc_t			inputFunc; 
	// For embedding additional datatables inside this one
	datamap_t			*td;

	// Stores the actual member variable size in bytes
	int					fieldSizeInBytes;

	// FTYPEDESC_OVERRIDE point to first baseclass instance if chains_validated has occurred
	class typedescription_t *override_field;

	// Used to track exclusion of baseclass fields
	int					override_count;

	// Tolerance for field errors for float fields
	float				fieldTolerance;
};

static int g_FieldSizes[FIELD_TYPECOUNT] =
{
	FIELD_SIZE(FIELD_VOID),
	FIELD_SIZE(FIELD_FLOAT),
	FIELD_SIZE(FIELD_STRING),
	FIELD_SIZE(FIELD_VECTOR),
	FIELD_SIZE(FIELD_QUATERNION),
	FIELD_SIZE(FIELD_INTEGER),
	FIELD_SIZE(FIELD_BOOLEAN),
	FIELD_SIZE(FIELD_SHORT),
	FIELD_SIZE(FIELD_CHARACTER),
	FIELD_SIZE(FIELD_COLOR32),
	FIELD_SIZE(FIELD_EMBEDDED),
	FIELD_SIZE(FIELD_CUSTOM),

	FIELD_SIZE(FIELD_CLASSPTR),
	FIELD_SIZE(FIELD_EHANDLE),
	FIELD_SIZE(FIELD_EDICT),

	FIELD_SIZE(FIELD_POSITION_VECTOR),
	FIELD_SIZE(FIELD_TIME),
	FIELD_SIZE(FIELD_TICK),
	FIELD_SIZE(FIELD_MODELNAME),
	FIELD_SIZE(FIELD_SOUNDNAME),

	FIELD_SIZE(FIELD_INPUT),
	FIELD_SIZE(FIELD_FUNCTION),
	FIELD_SIZE(FIELD_VMATRIX),
	FIELD_SIZE(FIELD_VMATRIX_WORLDSPACE),
	FIELD_SIZE(FIELD_MATRIX3X4_WORLDSPACE),
	FIELD_SIZE(FIELD_INTERVAL),
	FIELD_SIZE(FIELD_MODELINDEX),
	FIELD_SIZE(FIELD_MATERIALINDEX),

	FIELD_SIZE(FIELD_VECTOR2D),
	FIELD_SIZE(FIELD_INTEGER64),
	FIELD_SIZE(FIELD_POINTER),
};

typedef string_t(*AllocStringFunc)(const char* pszValue);
struct SaveRestoreFieldInfo_t
{
	void* pField;

	// Note that it is legal for the following two fields to be NULL,
	// though it may be disallowed by implementors of ISaveRestoreOps
	void* pOwner;
	typedescription_t* pTypeDesc;
};

class ISave;
class IRestore;
abstract_class ISaveRestoreOps
{
public:
	// save data type interface
	virtual void Save(const SaveRestoreFieldInfo_t & fieldInfo, ISave * pSave) = 0;
	virtual void Restore(const SaveRestoreFieldInfo_t& fieldInfo, IRestore* pRestore) = 0;

	virtual bool IsEmpty(const SaveRestoreFieldInfo_t& fieldInfo) = 0;
	virtual void MakeEmpty(const SaveRestoreFieldInfo_t& fieldInfo) = 0;
	virtual bool Parse(const SaveRestoreFieldInfo_t& fieldInfo, char const* szValue) = 0;

	//---------------------------------

	void Save(void* pField, ISave* pSave) { SaveRestoreFieldInfo_t fieldInfo = { pField, NULL, NULL }; Save(fieldInfo, pSave); }
	void Restore(void* pField, IRestore* pRestore) { SaveRestoreFieldInfo_t fieldInfo = { pField, NULL, NULL }; Restore(fieldInfo, pRestore); }

	bool IsEmpty(void* pField) { SaveRestoreFieldInfo_t fieldInfo = { pField, NULL, NULL }; return IsEmpty(fieldInfo); }
	void MakeEmpty(void* pField) { SaveRestoreFieldInfo_t fieldInfo = { pField, NULL, NULL }; MakeEmpty(fieldInfo); }
	bool Parse(void* pField, char const* pszValue) { SaveRestoreFieldInfo_t fieldInfo = { pField, NULL, NULL }; return Parse(fieldInfo, pszValue); }
};

//-----------------------------------------------------------------------------
// Purpose: stores the list of objects in the hierarchy
//			used to iterate through an object's data descriptions
//-----------------------------------------------------------------------------
class datamap_t
{
public:
	typedescription_t	*dataDesc;
	int					dataNumFields;
	char const			*dataClassName;
	datamap_t			*baseMap;

	bool				chains_validated;
	// Have the "packed" offsets been computed
	bool				packed_offsets_computed;
	int					packed_size;

#if defined( _DEBUG )
	bool				bValidityChecked;
#endif // _DEBUG

	//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
	void ComputePackedOffsets(void)
	{
#if !defined( NO_ENTITY_PREDICTION )
		if (this->packed_offsets_computed)
			return;

		this->ComputePackedSize_R();

		Assert(this->packed_offsets_computed);
#endif
	}

	//-----------------------------------------------------------------------------
	// Purpose: 
	// Output : int
	//-----------------------------------------------------------------------------
	int GetIntermediateDataSize(void)
	{
#if !defined( NO_ENTITY_PREDICTION )
		ComputePackedOffsets();

		Assert(this->packed_offsets_computed);

		int size = this->packed_size;

		Assert(size > 0);

		// At least 4 bytes to avoid some really bad stuff
		return MAX(size, 4);
#else
		return 0;
#endif
	}
	//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *map - 
// Output : int
//-----------------------------------------------------------------------------
	int ComputePackedSize_R()
	{
		// Already computed
		if (this->packed_offsets_computed)
		{
			return this->packed_size;
		}

		int current_position = 0;

		// Recurse to base classes first...
		if (this->baseMap)
		{
			current_position += this->baseMap->ComputePackedSize_R();
		}

		int c = this->dataNumFields;
		int i;
		typedescription_t* field;

		for (i = 0; i < c; i++)
		{
			field = &this->dataDesc[i];

			// Always descend into embedded types...
			if (field->fieldType != FIELD_EMBEDDED)
			{
				// Skip all private fields
				if (field->flags & FTYPEDESC_PRIVATE)
					continue;
			}

			switch (field->fieldType)
			{
			default:
			case FIELD_MODELINDEX:
			case FIELD_MODELNAME:
			case FIELD_SOUNDNAME:
			case FIELD_TIME:
			case FIELD_TICK:
			case FIELD_CUSTOM:
			case FIELD_CLASSPTR:
			case FIELD_EDICT:
			case FIELD_POSITION_VECTOR:
			case FIELD_FUNCTION:
				Assert(0);
				break;

			case FIELD_EMBEDDED:
			{
				Assert(field->td != NULL);

				int embeddedsize = field->td->ComputePackedSize_R();

				field->fieldOffset[TD_OFFSET_PACKED] = current_position;

				current_position += embeddedsize;
			}
			break;

			case FIELD_FLOAT:
			case FIELD_VECTOR:
			case FIELD_QUATERNION:
			case FIELD_INTEGER:
			case FIELD_EHANDLE:
			{
				// These should be dword aligned
				current_position = (current_position + 3) & ~3;
				field->fieldOffset[TD_OFFSET_PACKED] = current_position;
				Assert(field->fieldSize >= 1);
				current_position += g_FieldSizes[field->fieldType] * field->fieldSize;
			}
			break;

			case FIELD_SHORT:
			{
				// This should be word aligned
				current_position = (current_position + 1) & ~1;
				field->fieldOffset[TD_OFFSET_PACKED] = current_position;
				Assert(field->fieldSize >= 1);
				current_position += g_FieldSizes[field->fieldType] * field->fieldSize;
			}
			break;

			case FIELD_STRING:
			case FIELD_COLOR32:
			case FIELD_BOOLEAN:
			case FIELD_CHARACTER:
			{
				field->fieldOffset[TD_OFFSET_PACKED] = current_position;
				Assert(field->fieldSize >= 1);
				current_position += g_FieldSizes[field->fieldType] * field->fieldSize;
			}
			break;
			case FIELD_VOID:
			{
				// Special case, just skip it
			}
			break;
			}
		}

		this->packed_size = current_position;
		this->packed_offsets_computed = true;

		return current_position;
	}

	//-----------------------------------------------------------------------------
// Purpose: iterates through a typedescript data block, so it can insert key/value data into the block
// Input  : *pObject - pointer to the struct or class the data is to be insterted into
//			*pFields - description of the data
//			iNumFields - number of fields contained in pFields
//			char *szKeyName - name of the variable to look for
//			char *szValue - value to set the variable to
// Output : Returns true if the variable is found and set, false if the key is not found.
//-----------------------------------------------------------------------------
	bool ParseKeyvalue(void* pObject, const char* szKeyName, const char* szValue, AllocStringFunc func)//typedescription_t* pFields, int iNumFields, 
	{
		int i;
		typedescription_t* pField;

		for (i = 0; i < this->dataNumFields; i++)
		{
			pField = &this->dataDesc[i];

			int fieldOffset = pField->fieldOffset[TD_OFFSET_NORMAL];

			// Check the nested classes, but only if they aren't in array form.
			if ((pField->fieldType == FIELD_EMBEDDED) && (pField->fieldSize == 1))
			{
				for (datamap_t* dmap = pField->td; dmap != NULL; dmap = dmap->baseMap)
				{
					void* pEmbeddedObject = (void*)((char*)pObject + fieldOffset);
					if (dmap->ParseKeyvalue(pEmbeddedObject, szKeyName, szValue, func))
						return true;
				}
			}

			if ((pField->flags & FTYPEDESC_KEY) && !stricmp(pField->externalName, szKeyName))
			{
				switch (pField->fieldType)
				{
				case FIELD_MODELNAME:
				case FIELD_SOUNDNAME:
				case FIELD_STRING:
					(*(string_t*)((char*)pObject + fieldOffset)) = func(szValue);
					return true;

				case FIELD_TIME:
				case FIELD_FLOAT:
					(*(float*)((char*)pObject + fieldOffset)) = atof(szValue);
					return true;

				case FIELD_BOOLEAN:
					(*(bool*)((char*)pObject + fieldOffset)) = (bool)(atoi(szValue) != 0);
					return true;

				case FIELD_CHARACTER:
					(*(char*)((char*)pObject + fieldOffset)) = (char)atoi(szValue);
					return true;

				case FIELD_SHORT:
					(*(short*)((char*)pObject + fieldOffset)) = (short)atoi(szValue);
					return true;

				case FIELD_INTEGER:
				case FIELD_TICK:
					(*(int*)((char*)pObject + fieldOffset)) = atoi(szValue);
					return true;

				case FIELD_POSITION_VECTOR:
				case FIELD_VECTOR:
					UTIL_StringToVector((float*)((char*)pObject + fieldOffset), szValue);
					return true;

				case FIELD_VMATRIX:
				case FIELD_VMATRIX_WORLDSPACE:
					UTIL_StringToFloatArray((float*)((char*)pObject + fieldOffset), 16, szValue);
					return true;

				case FIELD_MATRIX3X4_WORLDSPACE:
					UTIL_StringToFloatArray((float*)((char*)pObject + fieldOffset), 12, szValue);
					return true;

				case FIELD_COLOR32:
					UTIL_StringToColor32((color32*)((char*)pObject + fieldOffset), szValue);
					return true;

				case FIELD_CUSTOM:
				{
					SaveRestoreFieldInfo_t fieldInfo =
					{
						(char*)pObject + fieldOffset,
						pObject,
						pField
					};
					pField->pSaveRestoreOps->Parse(fieldInfo, szValue);
					return true;
				}

				default:
				case FIELD_INTERVAL: // Fixme, could write this if needed
				case FIELD_CLASSPTR:
				case FIELD_MODELINDEX:
				case FIELD_MATERIALINDEX:
				case FIELD_EDICT:
					Warning("Bad field in entity!!\n");
					Assert(0);
					break;
				}
			}
		}

		return false;
	}

	//-----------------------------------------------------------------------------
// Purpose: iterates through a typedescript data block, so it can insert key/value data into the block
// Input  : *pObject - pointer to the struct or class the data is to be insterted into
//			*pFields - description of the data
//			iNumFields - number of fields contained in pFields
//			char *szKeyName - name of the variable to look for
//			char *szValue - value to set the variable to
// Output : Returns true if the variable is found and set, false if the key is not found.
//-----------------------------------------------------------------------------
	bool ExtractKeyvalue(void* pObject, const char* szKeyName, char* szValue, int iMaxLen)//typedescription_t* pFields, int iNumFields, 
	{
		int i;
		typedescription_t* pField;

		for (i = 0; i < this->dataNumFields; i++)
		{
			pField = &this->dataDesc[i];

			int fieldOffset = pField->fieldOffset[TD_OFFSET_NORMAL];

			// Check the nested classes, but only if they aren't in array form.
			if ((pField->fieldType == FIELD_EMBEDDED) && (pField->fieldSize == 1))
			{
				for (datamap_t* dmap = pField->td; dmap != NULL; dmap = dmap->baseMap)
				{
					void* pEmbeddedObject = (void*)((char*)pObject + fieldOffset);
					if (dmap->ExtractKeyvalue(pEmbeddedObject, szKeyName, szValue, iMaxLen))
						return true;
				}
			}

			if ((pField->flags & FTYPEDESC_KEY) && !stricmp(pField->externalName, szKeyName))
			{
				switch (pField->fieldType)
				{
				case FIELD_MODELNAME:
				case FIELD_SOUNDNAME:
				case FIELD_STRING:
					Q_strncpy(szValue, ((char*)pObject + fieldOffset), iMaxLen);
					return true;

				case FIELD_TIME:
				case FIELD_FLOAT:
					Q_snprintf(szValue, iMaxLen, "%f", (*(float*)((char*)pObject + fieldOffset)));
					return true;

				case FIELD_BOOLEAN:
					Q_snprintf(szValue, iMaxLen, "%d", (*(bool*)((char*)pObject + fieldOffset)) != 0);
					return true;

				case FIELD_CHARACTER:
					Q_snprintf(szValue, iMaxLen, "%d", (*(char*)((char*)pObject + fieldOffset)));
					return true;

				case FIELD_SHORT:
					Q_snprintf(szValue, iMaxLen, "%d", (*(short*)((char*)pObject + fieldOffset)));
					return true;

				case FIELD_INTEGER:
				case FIELD_TICK:
					Q_snprintf(szValue, iMaxLen, "%d", (*(int*)((char*)pObject + fieldOffset)));
					return true;

				case FIELD_POSITION_VECTOR:
				case FIELD_VECTOR:
					Q_snprintf(szValue, iMaxLen, "%f %f %f",
						((float*)((char*)pObject + fieldOffset))[0],
						((float*)((char*)pObject + fieldOffset))[1],
						((float*)((char*)pObject + fieldOffset))[2]);
					return true;

				case FIELD_VMATRIX:
				case FIELD_VMATRIX_WORLDSPACE:
					//UTIL_StringToFloatArray( (float *)((char *)pObject + fieldOffset), 16, szValue );
					return false;

				case FIELD_MATRIX3X4_WORLDSPACE:
					//UTIL_StringToFloatArray( (float *)((char *)pObject + fieldOffset), 12, szValue );
					return false;

				case FIELD_COLOR32:
					Q_snprintf(szValue, iMaxLen, "%d %d %d %d",
						((int*)((char*)pObject + fieldOffset))[0],
						((int*)((char*)pObject + fieldOffset))[1],
						((int*)((char*)pObject + fieldOffset))[2],
						((int*)((char*)pObject + fieldOffset))[3]);
					return true;

				case FIELD_CUSTOM:
				{
					/*
					SaveRestoreFieldInfo_t fieldInfo =
					{
						(char *)pObject + fieldOffset,
						pObject,
						pField
					};
					pField->pSaveRestoreOps->Parse( fieldInfo, szValue );
					*/
					return false;
				}

				default:
				case FIELD_INTERVAL: // Fixme, could write this if needed
				case FIELD_CLASSPTR:
				case FIELD_MODELINDEX:
				case FIELD_MATERIALINDEX:
				case FIELD_EDICT:
					Warning("Bad field in entity!!\n");
					Assert(0);
					break;
				}
			}
		}

		return false;
	}

	//-----------------------------------------------------------------------------
// Purpose: Search this datamap for the name of this member function
//			This is used to save/restore function pointers (convert pointer to text)
// Input  : *function - pointer to member function
// Output : const char * - function name
//-----------------------------------------------------------------------------
	const char* UTIL_FunctionToName(inputfunc_t function)//datamap_t* pMap, 
	{
		datamap_t* pMap = this;
		while (pMap)
		{
			for (int i = 0; i < pMap->dataNumFields; i++)
			{
				if (pMap->dataDesc[i].flags & FTYPEDESC_FUNCTIONTABLE)
				{
#ifdef WIN32
					Assert(sizeof(pMap->dataDesc[i].inputFunc) == sizeof(void*));
#elif defined(POSIX)
					Assert(sizeof(pMap->dataDesc[i].inputFunc) == 8);
#else
#error
#endif
					inputfunc_t pTest = pMap->dataDesc[i].inputFunc;

					if (pTest == function)
						return pMap->dataDesc[i].fieldName;
				}
			}
			pMap = pMap->baseMap;
		}

		return NULL;
	}

	// Misc useful
	static bool FStrEq(const char* sz1, const char* sz2)
	{
		return (sz1 == sz2 || V_stricmp(sz1, sz2) == 0);
	}
	//-----------------------------------------------------------------------------
// Purpose: Search the datamap for a function named pName
//			This is used to save/restore function pointers (convert text back to pointer)
// Input  : *pName - name of the member function
//-----------------------------------------------------------------------------
	inputfunc_t UTIL_FunctionFromName(const char* pName)
	{
		datamap_t* pMap = this;
		while (pMap)
		{
			for (int i = 0; i < pMap->dataNumFields; i++)
			{
#ifdef WIN32
				Assert(sizeof(pMap->dataDesc[i].inputFunc) == sizeof(void*));
#elif defined(POSIX)
				Assert(sizeof(pMap->dataDesc[i].inputFunc) == 8);
#else
#error
#endif

				if (pMap->dataDesc[i].flags & FTYPEDESC_FUNCTIONTABLE)
				{
					if (FStrEq(pName, pMap->dataDesc[i].fieldName))
					{
						return pMap->dataDesc[i].inputFunc;
					}
				}
			}
			pMap = pMap->baseMap;
		}

		Msg("Failed to find function %s\n", pName);

		return NULL;
	}

	static void UTIL_StringToFloatArray(float* pVector, int count, const char* pString)
	{
		char* pstr, * pfront, tempString[128];
		int	j;

		Q_strncpy(tempString, pString, sizeof(tempString));
		pstr = pfront = tempString;

		for (j = 0; j < count; j++)			// lifted from pr_edict.c
		{
			pVector[j] = atof(pfront);

			// skip any leading whitespace
			while (*pstr && *pstr <= ' ')
				pstr++;

			// skip to next whitespace
			while (*pstr && *pstr > ' ')
				pstr++;

			if (!*pstr)
				break;

			pstr++;
			pfront = pstr;
		}
		for (j++; j < count; j++)
		{
			pVector[j] = 0;
		}
	}

	static void UTIL_StringToVector(float* pVector, const char* pString)
	{
		UTIL_StringToFloatArray(pVector, 3, pString);
	}

	static void UTIL_StringToIntArray(int* pVector, int count, const char* pString)
	{
		char* pstr, * pfront, tempString[128];
		int	j;

		Q_strncpy(tempString, pString, sizeof(tempString));
		pstr = pfront = tempString;

		for (j = 0; j < count; j++)			// lifted from pr_edict.c
		{
			pVector[j] = atoi(pfront);

			while (*pstr && *pstr != ' ')
				pstr++;
			if (!*pstr)
				break;
			pstr++;
			pfront = pstr;
		}

		for (j++; j < count; j++)
		{
			pVector[j] = 0;
		}
	}

	static void UTIL_StringToColor32(color32* color, const char* pString)
	{
		int tmp[4];
		UTIL_StringToIntArray(tmp, 4, pString);
		color->r = tmp[0];
		color->g = tmp[1];
		color->b = tmp[2];
		color->a = tmp[3];
	}
};


//-----------------------------------------------------------------------------
//
// Macros used to implement datadescs
//
#define DECLARE_SIMPLE_DATADESC() \
	static datamap_t m_DataMap; \
	static datamap_t *GetBaseMap(); \
	template <typename T> friend void DataMapAccess(T *, datamap_t **p); \
	template <typename T> friend datamap_t *DataMapInit(T *);

#define	DECLARE_DATADESC() \
	DECLARE_SIMPLE_DATADESC() \
	virtual datamap_t *GetDataDescMap( void ) const;

#define BEGIN_DATADESC( className ) \
	datamap_t className::m_DataMap = { 0, 0, #className, NULL }; \
	datamap_t *className::GetDataDescMap( void ) const { return &m_DataMap; } \
	datamap_t *className::GetBaseMap() { datamap_t *pResult; DataMapAccess((BaseClass *)NULL, &pResult); return pResult; } \
	BEGIN_DATADESC_GUTS( className )

#define BEGIN_DATADESC_NO_BASE( className ) \
	datamap_t className::m_DataMap = { 0, 0, #className, NULL }; \
	datamap_t *className::GetDataDescMap( void ) const { return &m_DataMap; } \
	datamap_t *className::GetBaseMap() { return NULL; } \
	BEGIN_DATADESC_GUTS( className )

#define BEGIN_SIMPLE_DATADESC( className ) \
	datamap_t className::m_DataMap = { 0, 0, #className, NULL }; \
	datamap_t *className::GetBaseMap() { return NULL; } \
	BEGIN_DATADESC_GUTS( className )

#define BEGIN_SIMPLE_DATADESC_( className, BaseClass ) \
	datamap_t className::m_DataMap = { 0, 0, #className, NULL }; \
	datamap_t *className::GetBaseMap() { datamap_t *pResult; DataMapAccess((BaseClass *)NULL, &pResult); return pResult; } \
	BEGIN_DATADESC_GUTS( className )

#define BEGIN_DATADESC_GUTS( className ) \
	template <typename T> datamap_t *DataMapInit(T *); \
	template <> datamap_t *DataMapInit<className>( className * ); \
	namespace className##_DataDescInit \
	{ \
		datamap_t *g_DataMapHolder = DataMapInit( (className *)NULL ); /* This can/will be used for some clean up duties later */ \
	} \
	\
	template <> datamap_t *DataMapInit<className>( className * ) \
	{ \
		typedef className classNameTypedef; \
		static CDatadescGeneratedNameHolder nameHolder(#className); \
		className::m_DataMap.baseMap = className::GetBaseMap(); \
		static typedescription_t dataDesc[] = \
		{ \
		{ FIELD_VOID,"", {0,0},0,0,0,0,0,0}, /* so you can define "empty" tables */

#define END_DATADESC() \
		}; \
		\
		if ( sizeof( dataDesc ) > sizeof( dataDesc[0] ) ) \
		{ \
			classNameTypedef::m_DataMap.dataNumFields = SIZE_OF_ARRAY( dataDesc ) - 1; \
			classNameTypedef::m_DataMap.dataDesc 	  = &dataDesc[1]; \
		} \
		else \
		{ \
			classNameTypedef::m_DataMap.dataNumFields = 1; \
			classNameTypedef::m_DataMap.dataDesc 	  = dataDesc; \
		} \
		return &classNameTypedef::m_DataMap; \
	}

// used for when there is no data description
#define IMPLEMENT_NULL_SIMPLE_DATADESC( derivedClass ) \
	BEGIN_SIMPLE_DATADESC( derivedClass ) \
	END_DATADESC()

#define IMPLEMENT_NULL_SIMPLE_DATADESC_( derivedClass, baseClass ) \
	BEGIN_SIMPLE_DATADESC_( derivedClass, baseClass ) \
	END_DATADESC()

#define IMPLEMENT_NULL_DATADESC( derivedClass ) \
	BEGIN_DATADESC( derivedClass ) \
	END_DATADESC()

// helps get the offset of a bitfield
#define BEGIN_BITFIELD( name ) \
	union \
	{ \
		char name; \
		struct \
		{

#define END_BITFIELD() \
		}; \
	};

//-----------------------------------------------------------------------------
// Forward compatability with potential seperate byteswap datadescs

#define DECLARE_BYTESWAP_DATADESC() DECLARE_SIMPLE_DATADESC()
#define BEGIN_BYTESWAP_DATADESC(name) BEGIN_SIMPLE_DATADESC(name) 
#define BEGIN_BYTESWAP_DATADESC_(name,base) BEGIN_SIMPLE_DATADESC_(name,base) 
#define END_BYTESWAP_DATADESC() END_DATADESC()

//-----------------------------------------------------------------------------

template <typename T> 
inline void DataMapAccess(T *ignored, datamap_t **p)
{
	*p = &T::m_DataMap;
}

//-----------------------------------------------------------------------------

class CDatadescGeneratedNameHolder
{
public:
	CDatadescGeneratedNameHolder( const char *pszBase )
	 : m_pszBase(pszBase)
	{
		m_nLenBase = strlen( m_pszBase );
	}
	
	~CDatadescGeneratedNameHolder()
	{
		for ( int i = 0; i < m_Names.Count(); i++ )
		{
			delete[] m_Names[i];
		}
	}
	
	const char *GenerateName( const char *pszIdentifier )
	{
		char *pBuf = new char[m_nLenBase + strlen(pszIdentifier) + 1];
		strcpy( pBuf, m_pszBase );
		strcat( pBuf, pszIdentifier );
		m_Names.AddToTail( pBuf );
		return pBuf;
	}
	
private:
	const char *m_pszBase;
	size_t m_nLenBase;
	CUtlVector<char *> m_Names;
};

//-----------------------------------------------------------------------------

#ifndef GAME_DLL
#ifndef NO_ENTITY_PREDICTION
#define DECLARE_PREDICTABLE()											\
	public:																\
		static typedescription_t m_PredDesc[];							\
		static datamap_t m_PredMap;										\
		virtual datamap_t *GetPredDescMap( void ) const;						\
		template <typename T> friend datamap_t *PredMapInit(T *)
#else
#define DECLARE_PREDICTABLE()	template <typename T> friend datamap_t *PredMapInit(T *)
#endif

#ifndef NO_ENTITY_PREDICTION
#define BEGIN_PREDICTION_DATA( className ) \
	datamap_t className::m_PredMap = { 0, 0, #className, &BaseClass::m_PredMap }; \
	datamap_t *className::GetPredDescMap( void ) const { return &m_PredMap; } \
	BEGIN_PREDICTION_DATA_GUTS( className )

#define BEGIN_PREDICTION_DATA_NO_BASE( className ) \
	datamap_t className::m_PredMap = { 0, 0, #className, NULL }; \
	datamap_t *className::GetPredDescMap( void ) const { return &m_PredMap; } \
	BEGIN_PREDICTION_DATA_GUTS( className )

#define BEGIN_PREDICTION_DATA_GUTS( className ) \
	template <typename T> datamap_t *PredMapInit(T *); \
	template <> datamap_t *PredMapInit<className>( className * ); \
	namespace className##_PredDataDescInit \
	{ \
		datamap_t *g_PredMapHolder = PredMapInit( (className *)NULL ); /* This can/will be used for some clean up duties later */ \
	} \
	\
	template <> datamap_t *PredMapInit<className>( className * ) \
	{ \
		typedef className classNameTypedef; \
		static typedescription_t predDesc[] = \
		{ \
		{ FIELD_VOID,0, {0,0},0,0,0,0,0,0}, /* so you can define "empty" tables */

#define END_PREDICTION_DATA() \
		}; \
		\
		if ( sizeof( predDesc ) > sizeof( predDesc[0] ) ) \
		{ \
			classNameTypedef::m_PredMap.dataNumFields = ARRAYSIZE( predDesc ) - 1; \
			classNameTypedef::m_PredMap.dataDesc 	  = &predDesc[1]; \
		} \
		else \
		{ \
			classNameTypedef::m_PredMap.dataNumFields = 1; \
			classNameTypedef::m_PredMap.dataDesc 	  = predDesc; \
		} \
		return &classNameTypedef::m_PredMap; \
	}
#else
#define BEGIN_PREDICTION_DATA( className ) \
	template <> inline datamap_t *PredMapInit<className>( className * ) \
	{ \
		if ( 0 ) \
		{ \
			typedef className classNameTypedef; \
			typedescription_t predDesc[] = \
			{ \
				{ FIELD_VOID,0, {0,0},0,0,0,0,0,0},

#define BEGIN_PREDICTION_DATA_NO_BASE( className ) BEGIN_PREDICTION_DATA( className )

#define END_PREDICTION_DATA() \
			}; \
			predDesc[0].flags = 0; /* avoid compiler warning of unused data */ \
		} \
	}
#endif
#else
// nothing, only client and engine has a prediction system
#define DECLARE_PREDICTABLE()	
#define BEGIN_PREDICTION_DATA( className ) 
#define END_PREDICTION_DATA() 
#endif // !GAME_DLL

#include "tier0/memdbgoff.h"

#endif // DATAMAP_H
