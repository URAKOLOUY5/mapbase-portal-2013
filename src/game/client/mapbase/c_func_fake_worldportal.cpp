//========= Mapbase - https://github.com/mapbase-source/source-sdk-2013 ============//
//
// Purpose: Recreates Portal 2 linked_portal_door functionality using SDK code only.
//			(basically a combination of point_camera and func_reflective_glass)
//
// $NoKeywords: $
//===========================================================================//
#include "cbase.h"
#include "view_shared.h"
#include "viewrender.h"
#include "c_func_fake_worldportal.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

IMPLEMENT_CLIENTCLASS_DT( C_FuncFakeWorldPortal, DT_FuncFakeWorldPortal, CFuncFakeWorldPortal )

	RecvPropEHandle( RECVINFO( m_hTargetPlane ) ),
	RecvPropVector( RECVINFO( m_PlaneAngles ) ),
	RecvPropInt( RECVINFO( m_iSkyMode ) ),
	RecvPropFloat( RECVINFO( m_flScale ) ),
	RecvPropString( RECVINFO( m_iszRenderTarget ) ),
	RecvPropEHandle( RECVINFO( m_hFogController ) ),

END_RECV_TABLE()

//-----------------------------------------------------------------------------
// Globals
//-----------------------------------------------------------------------------
C_EntityClassList<C_FuncFakeWorldPortal> g_FakeWorldPortalList;
template<> C_FuncFakeWorldPortal *C_EntityClassList<C_FuncFakeWorldPortal>::m_pClassList = NULL;

C_FuncFakeWorldPortal* GetFakeWorldPortalList()
{
	return g_FakeWorldPortalList.m_pClassList;
}


//-----------------------------------------------------------------------------
// Constructor, destructor
//-----------------------------------------------------------------------------
C_FuncFakeWorldPortal::C_FuncFakeWorldPortal()
{
	m_iszRenderTarget[0] = '\0';

	g_FakeWorldPortalList.Insert( this );
}

C_FuncFakeWorldPortal::~C_FuncFakeWorldPortal()
{
	g_FakeWorldPortalList.Remove( this );
}


bool C_FuncFakeWorldPortal::ShouldDraw()
{
	return true;
}


//-----------------------------------------------------------------------------
// Do we have a fake world portal in view?
//-----------------------------------------------------------------------------
C_FuncFakeWorldPortal *IsFakeWorldPortalInView( const CViewSetup& view, cplane_t &plane )
{
	// Early out if no cameras
	C_FuncFakeWorldPortal *pReflectiveGlass = GetFakeWorldPortalList();
	if ( !pReflectiveGlass )
		return NULL;

	Frustum_t frustum;
	GeneratePerspectiveFrustum( view.origin, view.angles, view.zNear, view.zFar, view.fov, view.m_flAspectRatio, frustum );

	cplane_t localPlane;
	Vector vecOrigin, vecWorld, vecDelta, vecForward;
	AngleVectors( view.angles, &vecForward, NULL, NULL );

	for ( ; pReflectiveGlass != NULL; pReflectiveGlass = pReflectiveGlass->m_pNext )
	{
		if ( pReflectiveGlass->IsDormant() )
			continue;

		if ( pReflectiveGlass->m_iViewHideFlags & (1 << CurrentViewID()) )
			continue;

		Vector vecMins, vecMaxs;
		pReflectiveGlass->GetRenderBoundsWorldspace( vecMins, vecMaxs );
		if ( R_CullBox( vecMins, vecMaxs, frustum ) )
			continue;

		const model_t *pModel = pReflectiveGlass->GetModel();
		const matrix3x4_t& mat = pReflectiveGlass->EntityToWorldTransform();

		int nCount = modelinfo->GetBrushModelPlaneCount( pModel );
		for ( int i = 0; i < nCount; ++i )
		{
			modelinfo->GetBrushModelPlane( pModel, i, localPlane, &vecOrigin );

			MatrixTransformPlane( mat, localPlane, plane );			// Transform to world space
			VectorTransform( vecOrigin, mat, vecWorld );
					 
			if ( view.origin.Dot( plane.normal ) <= plane.dist )	// Check for view behind plane
				continue;
			
			VectorSubtract( vecWorld, view.origin, vecDelta );		// Backface cull
			if ( vecDelta.Dot( plane.normal ) >= 0 )
				continue;

			// Must have valid plane
			if ( !pReflectiveGlass->m_hTargetPlane )
				continue;

			return pReflectiveGlass;
		}
	}

	return NULL;
}

//-----------------------------------------------------------------------------
// Iterates through fake world portals instead of just picking one
//-----------------------------------------------------------------------------
C_FuncFakeWorldPortal *NextFakeWorldPortal( C_FuncFakeWorldPortal *pStart, const CViewSetup& view, cplane_t &plane,
	const Frustum_t &frustum )
{
	// Early out if no cameras
	C_FuncFakeWorldPortal *pReflectiveGlass = NULL;
	if (!pStart)
		pReflectiveGlass = GetFakeWorldPortalList();
	else
		pReflectiveGlass = pStart->m_pNext;

	cplane_t localPlane;
	Vector vecOrigin, vecWorld, vecDelta;
	for ( ; pReflectiveGlass != NULL; pReflectiveGlass = pReflectiveGlass->m_pNext )
	{
		if ( pReflectiveGlass->IsDormant() )
			continue;

		if ( pReflectiveGlass->m_iViewHideFlags & (1 << CurrentViewID()) )
			continue;

		Vector vecMins, vecMaxs;
		pReflectiveGlass->GetRenderBoundsWorldspace( vecMins, vecMaxs );
		if ( R_CullBox( vecMins, vecMaxs, frustum ) )
			continue;

		const model_t *pModel = pReflectiveGlass->GetModel();
		const matrix3x4_t& mat = pReflectiveGlass->EntityToWorldTransform();

		int nCount = modelinfo->GetBrushModelPlaneCount( pModel );
		for ( int i = 0; i < nCount; ++i )
		{
			modelinfo->GetBrushModelPlane( pModel, i, localPlane, &vecOrigin );

			MatrixTransformPlane( mat, localPlane, plane );			// Transform to world space
			VectorTransform( vecOrigin, mat, vecWorld );
					 
			if ( view.origin.Dot( plane.normal ) <= plane.dist )	// Check for view behind plane
				continue;
			
			VectorSubtract( vecWorld, view.origin, vecDelta );		// Backface cull
			if ( vecDelta.Dot( plane.normal ) >= 0 )
				continue;

			// Must have valid plane
			if ( !pReflectiveGlass->m_hTargetPlane )
				continue;

			return pReflectiveGlass;
		}
	}

	return NULL;
}

void C_FuncFakeWorldPortal::OnDataChanged( DataUpdateType_t type )
{
	// Reset render texture
	m_pRenderTarget = NULL;

	// Reset fog
	m_pFog = NULL;

	return BaseClass::OnDataChanged( type );
}

extern ITexture *GetWaterReflectionTexture( void );

ITexture *C_FuncFakeWorldPortal::RenderTarget()
{
	if (m_iszRenderTarget[0] != '\0')
	{
		if (!m_pRenderTarget)
		{
			// We don't use a CTextureReference for this because we don't want to shut down the texture on removal/change
			m_pRenderTarget = materials->FindTexture( m_iszRenderTarget, TEXTURE_GROUP_RENDER_TARGET );
		}

		if (m_pRenderTarget)
			return m_pRenderTarget;
	}

	return GetWaterReflectionTexture();
}

fogparams_t *C_FuncFakeWorldPortal::GetFog()
{
	if (m_pFog)
		return m_pFog;

	if (m_hFogController)
	{
		C_FogController *pFogController = dynamic_cast<C_FogController*>(m_hFogController.Get());
		if (pFogController)
		{
			m_pFog = &pFogController->m_fog;
		}
		else
		{
			Warning("%s is not an env_fog_controller\n", m_hFogController->GetEntityName());
		}
	}

	return NULL;
}
