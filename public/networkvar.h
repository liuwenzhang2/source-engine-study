//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#ifndef NETWORKVAR_H
#define NETWORKVAR_H
#ifdef _WIN32
#pragma once
#endif


#include "tier0/dbg.h"
#include "convar.h"

#if defined( CLIENT_DLL ) || defined( GAME_DLL )
#include "basehandle.h"
#endif


#pragma warning( disable : 4284 ) // warning C4284: return type for 'CNetworkVarT<int>::operator ->' is 'int *' (ie; not a UDT or reference to a UDT.  Will produce errors if applied using infix notation)

#define MyOffsetOf( type, var ) ( (intp)&((type*)0)->var )

#ifdef _DEBUG
extern bool g_bUseNetworkVars;
#define CHECK_USENETWORKVARS if(g_bUseNetworkVars)
#else
#define CHECK_USENETWORKVARS // don't check for g_bUseNetworkVars
#endif



inline int InternalCheckDeclareClass(const char* pClassName, const char* pClassNameMatch, void* pTestPtr, void* pBasePtr)
{
	// This makes sure that casting from ThisClass to BaseClass works right. You'll get a compiler error if it doesn't
	// work at all, and you'll get a runtime error if you use multiple inheritance.
	Assert(pTestPtr == pBasePtr);

	// This is triggered by IMPLEMENT_SERVER_CLASS. It does DLLClassName::CheckDeclareClass( #DLLClassName ).
	// If they didn't do a DECLARE_CLASS in DLLClassName, then it'll be calling its base class's version
	// and the class names won't match.
	Assert((void*)pClassName == (void*)pClassNameMatch);
	return 0;
}


template <typename T>
inline int CheckDeclareClass_Access(T*, const char* pShouldBe)
{
	return T::CheckDeclareClass(pShouldBe);
}

#ifndef _STATIC_LINKED
#ifdef _MSC_VER
#if defined(_DEBUG) && (_MSC_VER > 1200 )
#define VALIDATE_DECLARE_CLASS 1
#endif
#endif
#endif

#ifdef  VALIDATE_DECLARE_CLASS

#define DECLARE_CLASS( className, baseClassName ) \
		typedef baseClassName BaseClass; \
		typedef className ThisClass; \
		template <typename T> friend int CheckDeclareClass_Access(T *, const char *pShouldBe); \
		static int CheckDeclareClass( const char *pShouldBe ) \
		{ \
			InternalCheckDeclareClass( pShouldBe, #className, (ThisClass*)0xFFFFF, (BaseClass*)(ThisClass*)0xFFFFF ); \
			return CheckDeclareClass_Access( (BaseClass *)NULL, #baseClassName ); \
		}

// Use this macro when you have a base class, but it's part of a library that doesn't use network vars
// or any of the things that use ThisClass or BaseClass.
#define DECLARE_CLASS_GAMEROOT( className, baseClassName ) \
		typedef baseClassName BaseClass; \
		typedef className ThisClass; \
		template <typename T> friend int CheckDeclareClass_Access(T *, const char *pShouldBe); \
		static int CheckDeclareClass( const char *pShouldBe ) \
		{ \
			return InternalCheckDeclareClass( pShouldBe, #className, (ThisClass*)0xFFFFF, (BaseClass*)(ThisClass*)0xFFFFF ); \
		}

	// Deprecated macro formerly used to work around VC++98 bug
#define DECLARE_CLASS_NOFRIEND( className, baseClassName ) \
		DECLARE_CLASS( className, baseClassName )

#define DECLARE_CLASS_NOBASE( className ) \
		typedef className ThisClass; \
		template <typename T> friend int CheckDeclareClass_Access(T *, const char *pShouldBe); \
		static int CheckDeclareClass( const char *pShouldBe ) \
		{ \
			return InternalCheckDeclareClass( pShouldBe, #className, 0, 0 ); \
		} 

#else
#define DECLARE_CLASS( className, baseClassName ) \
		typedef baseClassName BaseClass; \
		typedef className ThisClass;

#define DECLARE_CLASS_GAMEROOT( className, baseClassName )	DECLARE_CLASS( className, baseClassName )
#define DECLARE_CLASS_NOFRIEND( className, baseClassName )	DECLARE_CLASS( className, baseClassName )

#define DECLARE_CLASS_NOBASE( className )\
		typedef className ThisClass;

#endif


#define DECLARE_EMBEDDED_NETWORKVAR() \
	template <typename U> friend int ServerClassInit(U *);	\
	template <typename U> friend int ClientClassInit(U *); \
	virtual void NetworkStateChanged() {}  \
	virtual void NetworkStateChanged( void *pProp ) {}

template<typename type, class T, int (*OffsetFun)(void), void (T::* ChangeFun)(void*)>
class NetworkVarEmbed : public type \
{
	template< class C >
	NetworkVarEmbed& operator=(const C& val) {
		*((type*)this) = val;
		return *this;
	}
public:
	void CopyFrom(const type& src) {
		*((type*)this) = src;
		NetworkStateChanged();
	}
	virtual void NetworkStateChanged() {
		CHECK_USENETWORKVARS((T*)(((char*)this) - OffsetFun()))->NetworkStateChanged();
	}
	virtual void NetworkStateChanged(void* pVar) {
		CHECK_USENETWORKVARS((T*)(((char*)this) - OffsetFun())->*ChangeFun)(pVar);
	}
};

// NOTE: Assignment operator is disabled because it doesn't call copy constructors of scalar types within the aggregate, so they are not marked changed
#define CNetworkVarEmbedded( type, name ) \
	static int GetOffsetOf_##name(){\
		return MyOffsetOf(ThisClass, name);\
	}\
	void NetworkStateChangedDispatch_##name( void *ptr ) \
	{ \
		NetworkStateChanged(ptr);\
	}\
	public:\
	NetworkVarEmbed<type, ThisClass, &GetOffsetOf_##name, &ThisClass::NetworkStateChangedDispatch_##name> name;


template<typename T>
FORCEINLINE void NetworkVarConstruct(T& x) { x = T(0); }
FORCEINLINE void NetworkVarConstruct(color32_s& x) { x.r = x.g = x.b = x.a = 0; }


#define IMPLEMENT_NETWORK_VAR_FOR_DERIVED( name ) \
		virtual void NetworkStateChanged_##name() { CHECK_USENETWORKVARS NetworkStateChanged(); } \
		virtual void NetworkStateChanged_##name( void *pVar ) { CHECK_USENETWORKVARS NetworkStateChanged( pVar ); }

#define DISABLE_NETWORK_VAR_FOR_DERIVED( name ) \
		virtual void NetworkStateChanged_##name() {} \
		virtual void NetworkStateChanged_##name( void *pVar ) {}

template< class Type, class T, int (*OffsetFun)(void), void (T::* ChangeFun)(void*), bool debug = false>
class CNetworkVarBase
{
public:
	inline CNetworkVarBase()
	{
		NetworkVarConstruct(m_Value);
	}

	template< class C >
	const Type& operator=(const C& val)
	{
		return Set((const Type)val);
	}

	template< class C >
	const Type& operator=(const CNetworkVarBase< C, T, OffsetFun, ChangeFun >& val)
	{
		return Set((const Type)val.m_Value);
	}

	const Type& Set(const Type& val)
	{
		if (memcmp(&m_Value, &val, sizeof(Type)))
		{
			NetworkStateChanged();
			m_Value = val;
		}
		return m_Value;
	}

	Type& GetForModify()
	{
		NetworkStateChanged();
		return m_Value;
	}

	template< class C >
	const Type& operator+=(const C& val)
	{
		return Set(m_Value + (const Type)val);
	}

	template< class C >
	const Type& operator-=(const C& val)
	{
		return Set(m_Value - (const Type)val);
	}

	template< class C >
	const Type& operator/=(const C& val)
	{
		return Set(m_Value / (const Type)val);
	}

	template< class C >
	const Type& operator*=(const C& val)
	{
		return Set(m_Value * (const Type)val);
	}

	template< class C >
	const Type& operator^=(const C& val)
	{
		return Set(m_Value ^ (const Type)val);
	}

	template< class C >
	const Type& operator|=(const C& val)
	{
		return Set(m_Value | (const Type)val);
	}

	const Type& operator++()
	{
		return (*this += 1);
	}

	Type operator--()
	{
		return (*this -= 1);
	}

	Type operator++(int) // postfix version..
	{
		Type val = m_Value;
		(*this += 1);
		return val;
	}

	Type operator--(int) // postfix version..
	{
		Type val = m_Value;
		(*this -= 1);
		return val;
	}

	// For some reason the compiler only generates type conversion warnings for this operator when used like 
	// CNetworkVarBase<unsigned char> = 0x1
	// (it warns about converting from an int to an unsigned char).
	template< class C >
	const Type& operator&=(const C& val)
	{
		return Set(m_Value & (const Type)val);
	}

	operator const Type& () const
	{
		return m_Value;
	}

	const Type& Get() const
	{
		return m_Value;
	}

	const Type* operator->() const
	{
		return &m_Value;
	}
	//#ifdef CLIENT_DLL

	//int noise[100];
	//#endif

	Type m_Value;

	//#ifdef CLIENT_DLL
	//int noise2[100];
	//#endif

protected:
	inline void NetworkStateChanged()
	{
		//Changer::NetworkStateChanged( this );
		CHECK_USENETWORKVARS((T*)(((char*)this) - OffsetFun())->*ChangeFun)(this);
	}
};

// Use this macro to define a network variable.
#define CNetworkVar( type, name ) \
	static int GetOffsetOf_##name(){\
		return MyOffsetOf(ThisClass, name);\
	}\
	void NetworkStateChangedDispatch_##name( void *ptr ) \
	{ \
		NetworkStateChanged(ptr);\
	}\
	public:\
	CNetworkVarBase<type, ThisClass, &GetOffsetOf_##name, &ThisClass::NetworkStateChangedDispatch_##name> name;
	
// Use this macro when you have a base class with a variable, and it doesn't have that variable in a SendTable,
// but a derived class does. Then, the entity is only flagged as changed when the variable is changed in
// an entity that wants to transmit the variable.
#define CNetworkVarForDerived( type, name ) \
	virtual void NetworkStateChanged_##name() {} \
	virtual void NetworkStateChanged_##name( void *pVar ) {} \
	static int GetOffsetOf_##name(){\
		return MyOffsetOf(ThisClass, name);\
	}\
	void NetworkStateChangedDispatch_##name( void *ptr ) \
	{ \
		NetworkStateChanged_##name(ptr);\
	}\
	public:\
	CNetworkVarBase<type, ThisClass, &GetOffsetOf_##name, &ThisClass::NetworkStateChangedDispatch_##name> name;
	
// This virtualizes the change detection on the variable, but it is ON by default.
// Use this when you have a base class in which MOST of its derived classes use this variable
// in their SendTables, but there are a couple that don't (and they
// can use DISABLE_NETWORK_VAR_FOR_DERIVED).
#define CNetworkVarForDerived_OnByDefault( type, name ) \
	virtual void NetworkStateChanged_##name() { CHECK_USENETWORKVARS NetworkStateChanged(); } \
	virtual void NetworkStateChanged_##name( void *pVar ) { CHECK_USENETWORKVARS NetworkStateChanged( pVar ); } \
	static int GetOffsetOf_##name(){\
		return MyOffsetOf(ThisClass, name);\
	}\
	void NetworkStateChangedDispatch_##name( void *ptr ) \
	{ \
		NetworkStateChanged_##name(ptr);\
	}\
	public:\
	CNetworkVarBase<type, ThisClass, &GetOffsetOf_##name, &ThisClass::NetworkStateChangedDispatch_##name> name;
	
// Use this macro to define a network variable.
#define CNetworkVar2( type, name ) \
	static int GetOffsetOf_##name(){\
		return MyOffsetOf(ThisClass, name);\
	}\
	void NetworkStateChangedDispatch_##name( void *ptr ) \
	{ \
		NetworkStateChanged(ptr);\
	}\
	public:\
	CNetworkVarBase<type, ThisClass, &GetOffsetOf_##name, &ThisClass::NetworkStateChangedDispatch_##name,true> name;
	
#define CNetworkVarForDerived2( type, name ) \
	virtual void NetworkStateChanged_##name() {} \
	virtual void NetworkStateChanged_##name( void *pVar ) {} \
	static int GetOffsetOf_##name(){\
		return MyOffsetOf(ThisClass, name);\
	}\
	void NetworkStateChangedDispatch_##name( void *ptr ) \
	{ \
		NetworkStateChanged_##name(ptr);\
	}\
	public:\
	CNetworkVarBase<type, ThisClass, &GetOffsetOf_##name, &ThisClass::NetworkStateChangedDispatch_##name,true> name;
	
template< class Type, class T, int (*OffsetFun)(void), void (T::* ChangeFun)(void*), bool debug = false >
class CNetworkColor32Base : public CNetworkVarBase< Type, T, OffsetFun, ChangeFun, debug >
{
	typedef CNetworkVarBase< Type, T, OffsetFun, ChangeFun > NetworkVarBaseClass;
public:
	inline void Init(byte rVal, byte gVal, byte bVal)
	{
		SetR(rVal);
		SetG(gVal);
		SetB(bVal);
	}
	inline void Init(byte rVal, byte gVal, byte bVal, byte aVal)
	{
		SetR(rVal);
		SetG(gVal);
		SetB(bVal);
		SetA(aVal);
	}

	const Type& operator=(const Type& val)
	{
		return this->Set(val);
	}

	const Type& operator=(const CNetworkColor32Base< Type, T, OffsetFun, ChangeFun >& val)
	{
		return NetworkVarBaseClass::Set(val.m_Value);
	}


	inline byte GetR() const { return NetworkVarBaseClass::m_Value.r; }
	inline byte GetG() const { return NetworkVarBaseClass::m_Value.g; }
	inline byte GetB() const { return NetworkVarBaseClass::m_Value.b; }
	inline byte GetA() const { return NetworkVarBaseClass::m_Value.a; }
	inline void SetR(byte val) { SetVal(NetworkVarBaseClass::m_Value.r, val); }
	inline void SetG(byte val) { SetVal(NetworkVarBaseClass::m_Value.g, val); }
	inline void SetB(byte val) { SetVal(NetworkVarBaseClass::m_Value.b, val); }
	inline void SetA(byte val) { SetVal(NetworkVarBaseClass::m_Value.a, val); }

protected:
	inline void SetVal(byte& out, const byte& in)
	{
		if (out != in)
		{
			NetworkVarBaseClass::NetworkStateChanged();
			out = in;
		}
	}
};

// Helper for color32's. Contains GetR(), SetR(), etc.. functions.
#define CNetworkColor32( name ) \
	static int GetOffsetOf_##name(){\
		return MyOffsetOf(ThisClass, name);\
	}\
	void NetworkStateChangedDispatch_##name( void *ptr ) \
	{ \
		NetworkStateChanged(ptr);\
	}\
	public:\
	CNetworkColor32Base<color32, ThisClass, &GetOffsetOf_##name, &ThisClass::NetworkStateChangedDispatch_##name> name;
	
#define CNetworkColor322( name ) \
	static int GetOffsetOf_##name(){\
		return MyOffsetOf(ThisClass, name);\
	}\
	void NetworkStateChangedDispatch_##name( void *ptr ) \
	{ \
		NetworkStateChanged(ptr);\
	}\
	public:\
	CNetworkColor32Base<color32, ThisClass, &GetOffsetOf_##name, &ThisClass::NetworkStateChangedDispatch_##name,true> name;
	
// Network vector wrapper.
template< class Type, class T, int (*OffsetFun)(void), void (T::* ChangeFun)(void*), bool debug = false >
class CNetworkVectorBase : public CNetworkVarBase< Type, T, OffsetFun, ChangeFun, debug>
{
	typedef CNetworkVarBase< Type, T, OffsetFun, ChangeFun> NetworkVarBaseClass;
public:
	inline void Init(float ix = 0, float iy = 0, float iz = 0)
	{
		SetX(ix);
		SetY(iy);
		SetZ(iz);
	}

	const Type& operator=(const Type& val)
	{
		return NetworkVarBaseClass::Set(val);
	}

	const Type& operator=(const CNetworkVectorBase< Type, T, OffsetFun, ChangeFun>& val)
	{
		return NetworkVarBaseClass::Set(val.m_Value);
	}

	inline float GetX() const { return NetworkVarBaseClass::m_Value.x; }
	inline float GetY() const { return NetworkVarBaseClass::m_Value.y; }
	inline float GetZ() const { return NetworkVarBaseClass::m_Value.z; }
	inline float operator[](int i) const { return NetworkVarBaseClass::m_Value[i]; }

	inline void SetX(float val) { DetectChange(NetworkVarBaseClass::m_Value.x, val); }
	inline void SetY(float val) { DetectChange(NetworkVarBaseClass::m_Value.y, val); }
	inline void SetZ(float val) { DetectChange(NetworkVarBaseClass::m_Value.z, val); }
	inline void Set(int i, float val) { DetectChange(NetworkVarBaseClass::m_Value[i], val); }

	bool operator==(const Type& val) const
	{
		return NetworkVarBaseClass::m_Value == (Type)val;
	}

	bool operator!=(const Type& val) const
	{
		return NetworkVarBaseClass::m_Value != (Type)val;
	}

	const Type operator+(const Type& val) const
	{
		return NetworkVarBaseClass::m_Value + val;
	}

	const Type operator-(const Type& val) const
	{
		return NetworkVarBaseClass::m_Value - val;
	}

	const Type operator*(const Type& val) const
	{
		return NetworkVarBaseClass::m_Value * val;
	}

	const Type& operator*=(float val)
	{
		return NetworkVarBaseClass::Set(NetworkVarBaseClass::m_Value * val);
	}

	const Type operator*(float val) const
	{
		return NetworkVarBaseClass::m_Value * val;
	}

	const Type operator/(const Type& val) const
	{
		return NetworkVarBaseClass::m_Value / val;
	}

private:
	inline void DetectChange(float& out, float in)
	{
		if (out != in)
		{
			NetworkVarBaseClass::NetworkStateChanged();
			out = in;
		}
	}
};

// Vectors + some convenient helper functions.
#define CNetworkVector( name ) \
	static int GetOffsetOf_##name(){\
		return MyOffsetOf(ThisClass, name);\
	}\
	void NetworkStateChangedDispatch_##name( void *ptr ) \
	{ \
		NetworkStateChanged(ptr);\
	}\
	public:\
	CNetworkVectorBase<Vector, ThisClass, &GetOffsetOf_##name, &ThisClass::NetworkStateChangedDispatch_##name> name;

#define CNetworkVectorForDerived( name ) \
	virtual void NetworkStateChanged_##name() {} \
	virtual void NetworkStateChanged_##name( void *pVar ) {} \
	static int GetOffsetOf_##name(){\
		return MyOffsetOf(ThisClass, name);\
	}\
	void NetworkStateChangedDispatch_##name( void *ptr ) \
	{ \
		NetworkStateChanged_##name(ptr);\
	}\
	public:\
	CNetworkVectorBase<Vector, ThisClass, &GetOffsetOf_##name, &ThisClass::NetworkStateChangedDispatch_##name> name;

#define CNetworkVector2( name ) \
	static int GetOffsetOf_##name(){\
		return MyOffsetOf(ThisClass, name);\
	}\
	void NetworkStateChangedDispatch_##name( void *ptr ) \
	{ \
		NetworkStateChanged(ptr);\
	}\
	public:\
	CNetworkVectorBase<Vector, ThisClass, &GetOffsetOf_##name, &ThisClass::NetworkStateChangedDispatch_##name,true> name;

#define CNetworkVectorForDerived2( name ) \
	virtual void NetworkStateChanged_##name() {} \
	virtual void NetworkStateChanged_##name( void *pVar ) {} \
	static int GetOffsetOf_##name(){\
		return MyOffsetOf(ThisClass, name);\
	}\
	void NetworkStateChangedDispatch_##name( void *ptr ) \
	{ \
		NetworkStateChanged_##name(ptr);\
	}\
	public:\
	CNetworkVectorBase<Vector, ThisClass, &GetOffsetOf_##name, &ThisClass::NetworkStateChangedDispatch_##name,true> name;

#define CNetworkQAngle( name ) \
	static int GetOffsetOf_##name(){\
		return MyOffsetOf(ThisClass, name);\
	}\
	void NetworkStateChangedDispatch_##name( void *ptr ) \
	{ \
		NetworkStateChanged(ptr);\
	}\
	public:\
	CNetworkVectorBase<QAngle, ThisClass, &GetOffsetOf_##name, &ThisClass::NetworkStateChangedDispatch_##name> name;
	
#define CNetworkQAngle2( name ) \
	static int GetOffsetOf_##name(){\
		return MyOffsetOf(ThisClass, name);\
	}\
	void NetworkStateChangedDispatch_##name( void *ptr ) \
	{ \
		NetworkStateChanged(ptr);\
	}\
	public:\
	CNetworkVectorBase<QAngle, ThisClass, &GetOffsetOf_##name, &ThisClass::NetworkStateChangedDispatch_##name,true> name;

// Network vector wrapper.
template< class Type, class T, int (*OffsetFun)(void), void (T::* ChangeFun)(void*), bool debug = false >
class CNetworkQuaternionBase : public CNetworkVarBase< Type, T, OffsetFun, ChangeFun, debug >
{
	typedef CNetworkVarBase< Type, T, OffsetFun, ChangeFun > NetworkVarBaseClass;
public:
	inline void Init(float ix = 0, float iy = 0, float iz = 0, float iw = 0)
	{
		SetX(ix);
		SetY(iy);
		SetZ(iz);
		SetW(iw);
	}

	const Type& operator=(const Type& val)
	{
		return NetworkVarBaseClass::Set(val);
	}

	const Type& operator=(const CNetworkQuaternionBase< Type, T, OffsetFun, ChangeFun >& val)
	{
		return NetworkVarBaseClass::Set(val.m_Value);
	}

	inline float GetX() const { return NetworkVarBaseClass::m_Value.x; }
	inline float GetY() const { return NetworkVarBaseClass::m_Value.y; }
	inline float GetZ() const { return NetworkVarBaseClass::m_Value.z; }
	inline float GetW() const { return NetworkVarBaseClass::m_Value.w; }
	inline float operator[](int i) const { return NetworkVarBaseClass::m_Value[i]; }

	inline void SetX(float val) { DetectChange(NetworkVarBaseClass::m_Value.x, val); }
	inline void SetY(float val) { DetectChange(NetworkVarBaseClass::m_Value.y, val); }
	inline void SetZ(float val) { DetectChange(NetworkVarBaseClass::m_Value.z, val); }
	inline void SetW(float val) { DetectChange(NetworkVarBaseClass::m_Value.w, val); }
	inline void Set(int i, float val) { DetectChange(NetworkVarBaseClass::m_Value[i], val); }

	bool operator==(const Type& val) const
	{
		return NetworkVarBaseClass::m_Value == (Type)val;
	}

	bool operator!=(const Type& val) const
	{
		return NetworkVarBaseClass::m_Value != (Type)val;
	}

	const Type operator+(const Type& val) const
	{
		return NetworkVarBaseClass::m_Value + val;
	}

	const Type operator-(const Type& val) const
	{
		return NetworkVarBaseClass::m_Value - val;
	}

	const Type operator*(const Type& val) const
	{
		return NetworkVarBaseClass::m_Value * val;
	}

	const Type& operator*=(float val)
	{
		return NetworkVarBaseClass::Set(NetworkVarBaseClass::m_Value * val);
	}

	const Type operator*(float val) const
	{
		return NetworkVarBaseClass::m_Value * val;
	}

	const Type operator/(const Type& val) const
	{
		return NetworkVarBaseClass::m_Value / val;
	}

private:
	inline void DetectChange(float& out, float in)
	{
		if (out != in)
		{
			NetworkVarBaseClass::NetworkStateChanged();
			out = in;
		}
	}
};

#define CNetworkQuaternion( name ) \
	static int GetOffsetOf_##name(){\
		return MyOffsetOf(ThisClass, name);\
	}\
	void NetworkStateChangedDispatch_##name( void *ptr ) \
	{ \
		NetworkStateChanged(ptr);\
	}\
	public:\
	CNetworkQuaternionBase<Quaternion, ThisClass, &GetOffsetOf_##name, &ThisClass::NetworkStateChangedDispatch_##name> name;


template< class Type, class T, int (*OffsetFun)(void), void (T::* ChangeFun)(void*), bool debug = false >
class CNetworkStringTBase : public CNetworkVarBase< Type, T, OffsetFun, ChangeFun, debug >
{
	typedef CNetworkVarBase< Type, T, OffsetFun, ChangeFun > NetworkVarBaseClass;
public:

	const char* ToCStr() {
		return NetworkVarBaseClass::m_Value.ToCStr();
	}

	const Type& operator=(const Type& val)
	{
		return NetworkVarBaseClass::Set(val);
	}

	const Type& operator=(const CNetworkStringTBase< Type, T, OffsetFun, ChangeFun >& val)
	{
		return NetworkVarBaseClass::Set(val.m_Value);
	}
};

#define CNetworkStringT( name ) \
	static int GetOffsetOf_##name(){\
		return MyOffsetOf(ThisClass, name);\
	}\
	void NetworkStateChangedDispatch_##name( void *ptr ) \
	{ \
		NetworkStateChanged(ptr);\
	}\
	public:\
	CNetworkStringTBase<string_t, ThisClass, &GetOffsetOf_##name, &ThisClass::NetworkStateChangedDispatch_##name> name;


// Network ehandle wrapper.
#if defined( CLIENT_DLL ) || defined( GAME_DLL )
inline void NetworkVarConstruct(CBaseHandle& x) {}

template< class Type, class T, int (*OffsetFun)(void), void (T::* ChangeFun)(void*), bool debug = false >
class CNetworkHandleBase : public CNetworkVarBase< CBaseHandle, T, OffsetFun, ChangeFun, debug >
{
	typedef CNetworkVarBase< CBaseHandle, T, OffsetFun, ChangeFun > NetworkVarBaseClass;
public:
	const Type* operator=(const Type* val)
	{
		return Set(val);
	}

	const Type& operator=(const CNetworkHandleBase< CBaseHandle, T, OffsetFun, ChangeFun >& val)
	{
		const CBaseHandle& handle = NetworkVarBaseClass::Set(val.m_Value);
		return *(const Type*)handle.Get();
	}

	bool operator !() const
	{
		return NetworkVarBaseClass::m_Value.Get();
	}

	operator Type* () const
	{
		return static_cast<Type*>(NetworkVarBaseClass::m_Value.Get());
	}

	//operator int() {
	//	return CNetworkVarBase<CBaseHandle, Changer>::m_Value.ToInt();
	//}

	const Type* Set(const Type* val)
	{
		if (NetworkVarBaseClass::m_Value != val)
		{
			NetworkVarBaseClass::NetworkStateChanged();
			NetworkVarBaseClass::m_Value = val;
		}
		return val;
	}

	Type* Get() const
	{
		return static_cast<Type*>(NetworkVarBaseClass::m_Value.Get());
	}

	bool IsValid() const {
		return NetworkVarBaseClass::m_Value.IsValid();
	}

	int ToInt() {
		return NetworkVarBaseClass::m_Value.ToInt();
	}

	int GetEntryIndex() const {
		return NetworkVarBaseClass::m_Value.GetEntryIndex();
	}
	int GetSerialNumber() const {
		return NetworkVarBaseClass::m_Value.GetSerialNumber();
	}

	void Init(int iEntity, int iSerialNum) {
		NetworkVarBaseClass::m_Value.Init(iEntity, iSerialNum);
	}

	void Term() {
		NetworkVarBaseClass::m_Value.Term();
	}

	Type* operator->() const
	{
		return static_cast<Type*>(NetworkVarBaseClass::m_Value.Get());
	}

	bool operator==(const Type* val) const
	{
		return NetworkVarBaseClass::m_Value == val;
	}

	bool operator!=(const Type* val) const
	{
		return NetworkVarBaseClass::m_Value != val;
	}
};

#define CNetworkHandle( type, name ) \
		static int GetOffsetOf_##name(){\
			return MyOffsetOf(ThisClass, name);\
		}\
		void NetworkStateChangedDispatch_##name( void *ptr ) \
		{ \
			NetworkStateChanged(ptr);\
		}\
		public:\
		CNetworkHandleBase<type, ThisClass, &GetOffsetOf_##name, &ThisClass::NetworkStateChangedDispatch_##name> name;

#define CNetworkHandleForDerived( type, name ) \
		virtual void NetworkStateChanged_##name() {} \
		virtual void NetworkStateChanged_##name( void *pVar ) {} \
		static int GetOffsetOf_##name(){\
			return MyOffsetOf(ThisClass, name);\
		}\
		void NetworkStateChangedDispatch_##name( void *ptr ) \
		{ \
			NetworkStateChanged_##name(ptr);\
		}\
		public:\
		CNetworkHandleBase<type, ThisClass, &GetOffsetOf_##name, &ThisClass::NetworkStateChangedDispatch_##name> name;

#define CNetworkHandle2( type, name ) \
		static int GetOffsetOf_##name(){\
			return MyOffsetOf(ThisClass, name);\
		}\
		void NetworkStateChangedDispatch_##name( void *ptr ) \
		{ \
			NetworkStateChanged(ptr);\
		}\
		public:\
		CNetworkHandleBase<type, ThisClass, &GetOffsetOf_##name, &ThisClass::NetworkStateChangedDispatch_##name,true> name;

#define CNetworkHandleForDerived2( type, name ) \
		virtual void NetworkStateChanged_##name() {} \
		virtual void NetworkStateChanged_##name( void *pVar ) {} \
		static int GetOffsetOf_##name(){\
			return MyOffsetOf(ThisClass, name);\
		}\
		void NetworkStateChangedDispatch_##name( void *ptr ) \
		{ \
			NetworkStateChanged_##name(ptr);\
		}\
		public:\
		CNetworkHandleBase<type, ThisClass, &GetOffsetOf_##name, &ThisClass::NetworkStateChangedDispatch_##name,true> name;

#endif

template<int length, class T, int (*OffsetFun)(void), bool debug = false>
class NetworkVarString {
public:
	NetworkVarString() {
		m_Value[0] = '\0';
	}
	operator const char* () const {
		return m_Value;
	}
	const char* Get() const {
		return m_Value;
	}
#ifdef CLIENT_DLL
	char& operator[](int i) {
		return m_Value[i];
	}
#endif // CLIENT_DLL
	char* GetForModify()
	{
		NetworkStateChanged();
		return m_Value;
	}
protected:
	inline void NetworkStateChanged()
	{
		CHECK_USENETWORKVARS((T*)(((char*)this) - OffsetFun()))->NetworkStateChanged();
	}
public:
	//int noise[100];
	char m_Value[length];
	//int noise2[100];
};


#define CNetworkString( name, length ) \
	static int GetOffsetOf_##name(){\
		return MyOffsetOf(ThisClass, name);\
	}\
	public:\
	NetworkVarString<length,ThisClass, &GetOffsetOf_##name> name;

template<typename type, int count, class T, int (*OffsetFun)(void), void (T::* ChangeFun)(void*), bool debug = false >
class CNetworkVarArray
{
public:
	inline CNetworkVarArray()
	{
		for (int i = 0; i < count; ++i) {
			NetworkVarConstruct(m_Value[i]);
		}
	}
	template <typename U> friend int ServerClassInit(U*);
	const type& operator[](int i) const
	{
		return Get(i);
	}

	const type& Get(int i) const
	{
		Assert(i >= 0 && i < count);
		return m_Value[i];
	}

#ifdef CLIENT_DLL
	type& operator[](int i)
	{
		return Get(i);
	}
	type& Get(int i)
	{
		Assert(i >= 0 && i < count);
		return m_Value[i];
	}
#endif
	type& GetForModify(int i)
	{
		Assert(i >= 0 && i < count);
		NetworkStateChanged(i);
		return m_Value[i];
	}

	void Set(int i, const type& val)
	{
		Assert(i >= 0 && i < count);
		if (memcmp(&m_Value[i], &val, sizeof(type)))
		{
			NetworkStateChanged(i);
			m_Value[i] = val;
		}
	}
	const type* Base() const { return m_Value; }
	int Count() const { return count; }
public:
	CNetworkVarArray* operator&() {
		return this;
	}
protected:
	inline void NetworkStateChanged(int net_change_index)
	{
		CHECK_USENETWORKVARS((T*)(((char*)this) - OffsetFun())->*ChangeFun)(&m_Value[net_change_index]);
	}
public:
	//int noise[100];
	type m_Value[count];
	//int noise2[100];
};

#define CNetworkArray( type, name, count )\
	static int GetOffsetOf_##name() {\
		return MyOffsetOf(ThisClass, name); \
	}\
	void NetworkStateChangedDispatch_##name(void* ptr) \
	{ \
		NetworkStateChanged(ptr); \
	}\
	public:\
	CNetworkVarArray<type, count, ThisClass, &GetOffsetOf_##name, &ThisClass::NetworkStateChangedDispatch_##name> name;

#define CNetworkArrayForDerived( type, name, count ) \
	virtual void NetworkStateChanged_##name() {} \
	virtual void NetworkStateChanged_##name( void *pVar ) {} \
	static int GetOffsetOf_##name(){\
		return MyOffsetOf(ThisClass, name);\
	}\
	void NetworkStateChangedDispatch_##name( void *ptr ) \
	{ \
		NetworkStateChanged_##name(ptr);\
	}\
	public:\
	CNetworkVarArray<type, count, ThisClass, &GetOffsetOf_##name, &ThisClass::NetworkStateChangedDispatch_##name> name;

#define CNetworkArray2( type, name, count )  \
	static int GetOffsetOf_##name() {\
		return MyOffsetOf(ThisClass, name); \
	}\
	void NetworkStateChangedDispatch_##name(void* ptr) \
	{ \
		NetworkStateChanged(ptr); \
	}\
	public:\
	CNetworkVarArray<type, count, ThisClass, &GetOffsetOf_##name, &ThisClass::NetworkStateChangedDispatch_##name,true> name;


#endif // NETWORKVAR_H
