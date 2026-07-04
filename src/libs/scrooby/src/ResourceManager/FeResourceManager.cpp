//===========================================================================
// Copyright (C) 2000 Radical Entertainment Ltd.  All rights reserved.
//
// Component:   FeResourceManager
//
// Description: implementation of the resource manager - this will coordinate
//                loading and unloading of all resources
//
// Authors:     IJG        10 JUL 2000
//
//===========================================================================

//===========================================================================
// Includes
//===========================================================================
#include <p3d/anim/multicontroller.hpp>
#include <p3d/camera.hpp>
#include <p3d/context.hpp>
#include <p3d/fileftt.hpp>
#include <p3d/ImageFactory.hpp>
#include <p3d/utility.hpp>
#include "FeApp.h"
#include "FePage.h"
#include "FeProject.h"
#include "resourcemanager/FeResourceManager.h"
#include "FeScreen.h"
#include "FeTextBible.h"
#include <string.h>
#include "utility/debugMessages.h"
#include <radmemory.hpp>
#include <radload/utility/object.hpp>
#include <p3d/inventory.hpp>

// TC: This is a quick hack for implementing the Simpsons2 FE holidays
//     theme.  At this point in the project, it's the easiest thing to
//     do.
//
#define SRR2_HOLIDAYS_THEME_HACK

#ifdef SRR2_HOLIDAYS_THEME_HACK
    #include <radtime.hpp>

    #if defined(RAD_WIN32) || defined(RAD_UWP)
        #define WIN32_EXTRA_LEAN
        #define WIN32_LEAN_AND_MEAN
        #include <Windows.h>
    #endif
#endif // SRR2_HOLIDAYS_THEME_HACK

//===========================================================================
// Global Data, Local Data, Local Classes
//===========================================================================

#ifdef SRR2_HOLIDAYS_THEME_HACK
    const char* FILENAME_SUFFIX_FOR_HALLOWEEN = "_H.p3d";
    const char* FILENAME_SUFFIX_FOR_THANKSGIVING = "_T.p3d";
    const char* FILENAME_SUFFIX_FOR_CHRISTMAS = "_C.p3d";
#endif

//===========================================================================
// Member Functions
//===========================================================================

//===========================================================================
// FeResourceManager()
//===========================================================================
// Description: constructor
//
// Constraints:    NONE
//
// Parameters:    None
//
// Return:      Nothing
//
//===========================================================================
FeResourceManager::FeResourceManager():
    mDeletedResources( 0 ),
    mDeletedAliases( 0 ),
    mDeletedAttributes( 0 ),
    m_pLoadProjectCallback( NULL ),
    m_pResourceManagerCallback( NULL ),
    m_pP3DCallback( NULL ),
    m_bAsyncLoading( true ),
    m_pLoadingProject( NULL ),
    m_Allocator (RADMEMORY_ALLOC_DEFAULT),
    m_eLang( Scrooby::XL_ENGLISH ),
    m_imageHandler( NULL ),
    m_loadType( tImageHandler::IMAGE ),
    mCallbackID( 0 )
{
    strcpy( m_pInventorySection, P3D_DEFAULT_INV_SECTION );
//    m_pInventorySection[ 0 ] = '\0';

    // Add a secondary inventory to search if we can't find resources
    // in the primary inventory
    strcpy( m_pSecondaryInventorySection, P3D_DEFAULT_INV_SECTION );
//    m_pSecondaryInventorySection[ 0 ] = '\0';

    m_pP3DCallback = new( radMemoryGetCurrentAllocator() ) P3DCallback();
    m_pP3DCallback->SetResourceManager( this );
    m_pP3DCallback->AddRef();

    // Pre-reserve space into these arrays so that they will remember which heap
    // they were first created in.
    //
    // This is a hack, and should really be solved by not having auto-grow vectors
    // in Scrooby.
    //
    mResources.Reserve (1024);
    mAliases.Reserve (1024);
    mP3DAttributes.Reserve (32);
}

//===========================================================================
// virtual ~FeResourceManager()
//===========================================================================
// Description: destructor
//
// Constraints:    NONE
//
// Parameters:    None
//
// Return:      Nothing
//
//===========================================================================
FeResourceManager::~FeResourceManager()
{
    UnloadAll();

    p3d::inventory->SelectSection( "default" );

    m_pP3DCallback->Release();
}

//===========================================================================
// int AddFilename
//===========================================================================
// Description: adds a resource to the resource manager for loading
//
// Constraints:    NONE
//
// Parameters:    filename - the file to be loaded
//              parent - object to be notified when this load is done
//              type - the resource type it is loading
//              inventoryName - name to look for in the inventory; uses filename 
//                              if blank
//
// Return:      integral position the new filename was added to in the list
//
//===========================================================================
int FeResourceManager::AddResource( const char* filename, FeEntity* parent, ResourceType type, const char* inventoryName )
{
    radMemoryAllocator old = ::radMemorySetCurrentAllocator( RADMEMORY_ALLOC_TEMP );
        PascalCString newFile = filename;
        newFile.Replace( '\\','/' );
    ::radMemorySetCurrentAllocator( old );

#ifdef SRR2_HOLIDAYS_THEME_HACK
    if( type == RT_P3D_OBJECT )
    {
        int filenameLength = strlen( filename );

        for( int i = filenameLength - 1; i >= 0; i-- )
        {
            // search for last backslash or forward slash in filename path
            //
            if( filename[ i ] == '\\' || filename[ i ] == '/' )
            {
                const char* filenameOnly = filename + i + 1;

                // only replace camset.p3d and homer.p3d files
                //
                if( strcmp( filenameOnly, "camset.p3d" ) == 0 ||
                    strcmp( filenameOnly, "homer.p3d" ) == 0 )
                {
                    char filenameModified[ 256 ];
                    strcpy( filenameModified, filename );
                    filenameModified[ filenameLength - 4 ] = '\0';

                    radDate today;
                    ::radTimeGetDate( &today );

                    if( today.m_Month == 10 && today.m_Day == 31 ) // hallowe'en
                    {
                        strcat( filenameModified, FILENAME_SUFFIX_FOR_HALLOWEEN );
                    }
                    else if( today.m_Month == 11 && today.m_Day > 21 && today.m_Day <= 28 &&
                             radTimeGetWeekday( today.m_Year, today.m_Month, today.m_Day ) == Weekday_Thursday ) // US thanksgiving
                    {
                        strcat( filenameModified, FILENAME_SUFFIX_FOR_THANKSGIVING );
                    }
                    else if( today.m_Month == 12 && today.m_Day == 25 ) // christmas
                    {
                        strcat( filenameModified, FILENAME_SUFFIX_FOR_CHRISTMAS );
                    }
                    else
                    {
                        rReleasePrintf( "Not a special holiday. Using regular P3D file:\n    %s\n", filename );

                        break;
                    }

                    newFile = filenameModified;
                    rReleasePrintf( "Replacing normal P3D file w/ special holidays file:\n    %s\n", filenameModified );
                }

                break;
            }
        }
    }
#endif // SRR2_HOLIDAYS_THEME_HACK

    ResourceEntry* newResource = new ResourceEntry();
    newResource->AddRef();
    if( inventoryName != NULL )
    {
        newResource->m_inventoryName = inventoryName;
        radMemoryAllocator old = ::radMemorySetCurrentAllocator( RADMEMORY_ALLOC_TEMP );
        PascalCString p3d = "p3d";
        if( !newFile.JustExtension().EqualsInsensitive( p3d ) )
        {
            newResource->m_inventoryName.Replace( '\\','/');
        }
        ::radMemorySetCurrentAllocator( old );
    }
    else
    {
        newResource->m_inventoryName = "";
    }
    newResource->SetName( newFile );
    newResource->m_parent = parent;
    newResource->m_type = type;

    newResource->m_Status = RM_NOT_LOADED;
    if( mDeletedResources )
    {
        int i = 0;
        for( i; i < mResources.Size(); i++ )
        {
            if( mResources[i] == NULL )
            {
                mResources[i] = newResource;
                mDeletedResources--;
                return i;
            }
        }
    }
    mResources.Insert( newResource );
    return mResources.Size()-1;
}

//===========================================================================
// int AddAlias
//===========================================================================
// Description: creates an alias for a resource
//
// Constraints:    NONE
//
// Parameters:    alias - name to associate with the index
//              index - index into the resource list
//
// Return:      integral position the new alias was added to in the list
//
//===========================================================================
int FeResourceManager::AddAlias( const tName& alias, int index )
{
    AliasEntry* newAlias = NULL;
    newAlias = new AliasEntry();
    newAlias->SetName( alias );
    newAlias->m_resourceIndex = index;
    if( mDeletedAliases )
    {
        int i = 0;
        for( i; i < mAliases.Size(); i++ )
        {
            if( mAliases[i] == NULL )
            {
                mAliases[i] = newAlias;
                mDeletedAliases--;
                return i;
            }
        }
    }
    mAliases.Insert( newAlias );
    return mAliases.Size();
}

//===========================================================================
// int AddPure3dAttributes
//===========================================================================
// Description: attach additional attributes to pure3d objects
//
// Constraints:    NONE
//
// Parameters:    camera - tCamera object name to use; will use default if empty
//              animation - tMultiController object to use; optional
//              index - index into the resource list
//
// Return:      integral position the attributes were added to in the list
//
//===========================================================================
int FeResourceManager::AddPure3dAttributes( const char* camera, const char* animation, int index )
{
    Pure3dAttributesEntry* newEntry = NULL;
    newEntry = new Pure3dAttributesEntry();
    newEntry->AddRef();
    if( camera )
    {
        newEntry->m_CameraName = camera;
    }
    if( animation )
    {
        newEntry->m_AnimName = animation;
    }
    newEntry->m_resourceIndex = index;
    if( mDeletedAttributes )
    {
        int i = 0;
        for( i; i < mP3DAttributes.Size(); i++ )
        {
            if( mP3DAttributes[i] == NULL )
            {
                mP3DAttributes[i] = newEntry;
                mDeletedAttributes--;
                return i;
            }
        }
    }
    mP3DAttributes.Insert( newEntry );
    return mP3DAttributes.Size();
}

//===========================================================================
// void ContinueLoading()
//===========================================================================
// Description: allows us to continue loading some stuff
//
// Constraints:    None
//
// Parameters:    NONE
//
// Return:      None
//
//===========================================================================
void FeResourceManager::ContinueLoading()
{
    //
    // Cycle through the list of files, and load those that are appropriate
    //

    radMemoryAllocator old = ::radMemorySetCurrentAllocator( GetAllocator() );

    int i = 0;

    for( i = 0; i < mResources.Size(); i++ )
    {
        ResourceEntry* resource = mResources[ i ];
#if defined(RAD_PSP)
        {
            extern int g_pspDiagFrame;   // >=0 on the first menu frames (harness)
            static int s_clLines = 0;
            if( g_pspDiagFrame >= 0 && s_clLines < 4000 )
            {
                FILE* cf = fopen("ms0:/shar_cl.log","a");
                if(cf){ fprintf(cf,"CL i=%d/%d %s st=%d ty=%d\n", i, mResources.Size(),
                        resource ? resource->GetName() : "(null)",
                        resource ? (int)resource->m_Status : -1,
                        resource ? (int)resource->m_type : -1 ); fclose(cf); }
                s_clLines++;
            }
        }
#endif
        if( resource )
        {
            if( resource->m_Status == RM_LOAD_COMPLETE )
            {
            }
            else if( (resource->m_Status == RM_P3D_LOAD_IN_PROGRESS) || (resource->m_Status == RM_NOT_LOADED) )
            {
                p3d::inventory->PushSection();
                p3d::inventory->SelectSection( m_pInventorySection );
                bool currentSectionOnly = p3d::inventory->GetCurrentSectionOnly();
                p3d::inventory->SetCurrentSectionOnly( true );
                tEntity* entity = NULL;
                if( mResources[i]->m_inventoryName == "" )
                {
                    entity = p3d::find<tEntity>( resource->GetName() );
                }
                else
                {
                    entity = p3d::find<tEntity>( resource->m_inventoryName );
                }

                if( entity == NULL )
                {
                    // try searching in secondary inventory section, if exists
                    //
                    if( m_pSecondaryInventorySection[ 0 ] != '\0' )
                    {
                        p3d::inventory->SelectSection( m_pSecondaryInventorySection );

                        if( mResources[i]->m_inventoryName == "" )
                        {
                            entity = p3d::find<tEntity>( resource->GetName() );
                        }
                        else
                        {
                            entity = p3d::find<tEntity>( resource->m_inventoryName );
                        }
                    }
                }

#if defined(RAD_PSP)
                // RT_IMAGE resources are referenced by full path (e.g.
                // "art/frontend/scrooby/resource/images/tvframe.png") but the
                // matching sprite loaded from frontend.p3d is named by BASENAME
                // ("tvframe.png"). Resolve by basename so we use the embedded
                // sprite instead of trying to open a non-existent external file
                // (which then crashes the PNG decoder on the failed/empty read).
                if( entity == NULL )
                {
                    const char* full = resource->GetName();
                    const char* base = full;
                    for( const char* p = full; *p; ++p )
                        if( *p == '/' || *p == '\\' ) base = p + 1;
                    if( base != full )
                    {
                        p3d::inventory->SelectSection( m_pInventorySection );
                        entity = p3d::find<tEntity>( base );
                    }
                }
#endif

                p3d::inventory->PopSection();
                p3d::inventory->SetCurrentSectionOnly( currentSectionOnly );
                if( entity != NULL )
                {
                    mResources[i]->theData = entity;
                    entity->AddRef();
                    mResources[i]->m_Status = RM_LOAD_COMPLETE;
                    if( mResources[i]->m_type == RT_PROJECT )
                    {
                        m_pLoadingProject = static_cast<FeProject*>( entity );
                        m_pLoadingProject->AddRef();
                        m_pLoadingProject->SetInventorySection( m_pInventorySection );
                        SetCallback( m_pLoadingProject );
                    }
                    if( (mResources[i]->m_type == RT_SCREEN) && (m_pLoadingProject) && (m_pLoadingProject->GetLastScreenToLoad() == i))
                    {
                        radMemoryAllocator old = ::radMemorySetCurrentAllocator( RADMEMORY_ALLOC_TEMP );
                            mCallbackID = AddResource( "ProjectCallback", NULL, RT_PROJECT_LOAD_CALLBACK );
                        ::radMemorySetCurrentAllocator( old );
                    }
                    if( mResources[i]->m_parent )
                    {
                        FeParent* parent = dynamic_cast<FeParent*>( mResources[i]->m_parent );
                        if( parent )
                        {
                            FeEntity* feEntity = dynamic_cast<FeEntity*>(entity);
                            if( feEntity )
                            {
                                parent->AddChild( feEntity );
                            }
                        }
                    }
                    if( mResources[i]->m_type == RT_TEXT_BIBLE )
                    {
                        FeTextBible* bible = dynamic_cast<FeTextBible*>( entity );
                        if( bible )
                        {
                            bible->SetLanguage( Scrooby::language_ID[ m_eLang ] );
                        }
                    }
                    //Scrooby::ScroobyMessage( Scrooby::LVL_DEBUG, "Load Complete : %s", mResources[i]->GetName() );
                }
                else
                {
                    if( mResources[i]->m_Status == RM_NOT_LOADED )
                    {
#if defined(RAD_PSP)
                        // NOTE: RT_P3D_OBJECT (the menu's 3D room-scene files:
                        // camset, homer, gag*, glow*, racecar, TVscreen) are now
                        // loaded — the animation subsystem and the camera/light/
                        // billboard/multicontroller loaders are registered, so
                        // these parse and render. A per-resource attempt counter
                        // below skips any single file that fails to resolve so a
                        // bad file can't block the whole project load.
                        if( mResources[i]->m_type == RT_IMAGE )
                        {
                            // RT_IMAGE resources are sprites embedded in
                            // frontend.p3d, resolved from the inventory by
                            // name/basename above. Reaching here means it wasn't
                            // found — the sprite was skipped (e.g. cards) or the
                            // external file is absent. Skip rather than load a
                            // missing file (crashes the PNG decoder) or spin
                            // forever waiting on a load that never resolves.
                            mResources[i]->m_Status = RM_LOAD_COMPLETE;
                            continue;
                        }
                        if( mResources[i]->m_type == RT_P3D_OBJECT )
                        {
                            // The menu shows Homer (gaghomer) doing a gag in the
                            // room; the other characters' gag files (mole, frink,
                            // barney, snake, nick, grandpa) are alternates the
                            // stock frontend swaps in on other menu states. On PSP
                            // we skip them — loading all seven full character
                            // composites at once (each ~90-140 KB of file, far
                            // more expanded in RAM: skeleton + 6 skins + meshes +
                            // textures) exhausts the heap before camset even
                            // loads. Keeping just gaghomer renders the visible
                            // scene within budget.
                            const char* full = mResources[i]->GetName();
                            const char* base = full;
                            for( const char* p = full; *p; ++p )
                                if( *p == '/' || *p == '\\' ) base = p + 1;
                            // Skip the alternate character gags (keep gaghomer),
                            // and skip the base homer.p3d — the menu also loads
                            // gaghomer (the animated Homer gag) at the same spot,
                            // so loading both draws two Homers. gaghomer is the
                            // one we want animated.
                            // MEMORY BUDGET (real PSP, ~16 MB user heap on this
                            // CFW+plugins config): the full frontend enumerates
                            // ~569 resources and the 3D scene set alone is ~12 MB
                            // of .p3d (expanded far larger in RAM) — it OOMs at
                            // ~15 MB mid-load and hard-powers-off. The main menu
                            // only shows ONE room + the animated Homer + glows +
                            // the TV + the iris wipe. Everything else (extra
                            // room variants, level-select maps, reward/podium
                            // screens, alternate characters, collectible cards)
                            // belongs to other frontend screens we don't run, so
                            // skip loading it. This is the fix for the OOM
                            // power-off, not just a per-file workaround.
                            //
                            // The dominant cost is FOUR ~2.2 MB camset variants
                            // (camset / _C / _H / _T ≈ 9 MB) — keep only the base
                            // camset.p3d the menu's CamAndSet uses.
                            bool skipHeavy =
                                // camset.p3d (incl. variants): the animated 3D room-scene background.
                                // Its parse HANGS the async load thread on PSP -- LoadFile never returns,
                                // so no further resource loads and the project never completes (menu never
                                // appears). Confirmed with BOTH optimized and pristine-original camset, so
                                // it is a pre-existing parser hang, not an asset problem. Skip loading it so
                                // the 2D menu comes up; the 3D background stays off until the hang is fixed.
                                ( strncmp( base, "camset", 6 ) == 0 &&
                  strcmp ( base, "camset.p3d" ) != 0 ) ||
                                // extra Homer skins (keep the animated gaghomer)
                                strncmp( base, "homer_", 6 ) == 0 ||
                                // other characters the main menu doesn't show
                                strcmp( base, "maggie.p3d" )  == 0 ||
                                // level-select preview maps (l1hudmap..l7hudmap)
                                strstr( base, "hudmap" ) != NULL ||
                                // reward / podium / level-select screen dressing
                                strncmp( base, "reward",  6 ) == 0 ||
                                strncmp( base, "curtain", 7 ) == 0 ||
                                strcmp( base, "podium.p3d" )   == 0 ||
                                strcmp( base, "pedestal.p3d" ) == 0 ||
                                strcmp( base, "sparkles.p3d" ) == 0;

                            bool skip = ( strncmp( base, "gag", 3 ) == 0 &&
                                          strcmp( base, "gaghomer.p3d" ) != 0 ) ||
                                        strcmp( base, "homer.p3d" ) == 0 ||
                                        // Collectible-card effects (cardsfx.p3d,
                                        // card*.p3d). The menu never shows cards
                                        // (their sprites are already skipped as
                                        // RT_IMAGE above), but cardsfx.p3d was
                                        // still being loaded as an RT_P3D_OBJECT.
                                        strncmp( base, "card", 4 ) == 0 ||
                                        skipHeavy;
                            if( skip )
                            {
                                FILE* rf = fopen("ms0:/shar_rm.log","a");
                                if(rf){ fprintf(rf,"skipping scene object: %s\n", base); fclose(rf); }
                                mResources[i]->m_Status = RM_LOAD_COMPLETE;
                                continue;
                            }
                        }
#endif
                        if( mResources[i]->m_type == RT_IMAGE )
                        {
                            // Grab any sort of image handler
                            PascalCString fileName = mResources[i]->GetName();
                            fileName = fileName.JustExtension();
                            if( fileName.EqualsInsensitive( "p3d" ) )
                            {

                            }
                            else
                            {
                                if( m_imageHandler )
                                {
                                    m_imageHandler->SetLoadType( m_loadType );
                                    m_imageHandler->Release();
                                }
                                m_imageHandler = dynamic_cast<tImageHandler*>(p3d::loadManager->GetHandler( fileName ) );
                                
                                // Set up our image loading parameters
                                if( m_imageHandler )
                                {
                                    m_imageHandler->AddRef();
                                    m_imageHandler->SetFullName( FeApp::GetInstance()->UseFullImageNames() ); 
                                    m_loadType = m_imageHandler->GetLoadType();
                                    m_imageHandler->SetLoadType( tImageHandler::SPRITE );
                                    m_imageHandler->SetNativeResolution( static_cast<int>(FeApp::GetInstance()->GetScreenWidth()), static_cast<int>(FeApp::GetInstance()->GetScreenHeight()) );
                                }
                            }
                        }

                        //Scrooby::Log::Message( Scrooby::LVL_INFO, "Loading file %s", mResources[i]->GetName() );
                        mResources[i]->m_Status = RM_P3D_LOAD_IN_PROGRESS;

                        const char* name = mResources[i]->GetName();
                        radMemoryAllocator old = ::radMemorySetCurrentAllocator( RADMEMORY_ALLOC_TEMP );
                        tLoadRequest* request = new tLoadRequest( name );
                        ::radMemorySetCurrentAllocator( old );
                        rAssert( request != NULL );

                        request->SetCallback( m_pP3DCallback );
                        request->SetInventorySection( m_pInventorySection );
                        request->SetAsync( m_bAsyncLoading );
                        request->SetMemoryAllocator( GetAllocator() );

                        if( mResources[i]->m_type == RT_PROJECT_LOAD_CALLBACK )
                        {
                            request->SetDummy( true );
                            mResources[i]->m_Status = RM_LOAD_COMPLETE;

                            // restore old allocator cuz we're done loading
                            //
                            ::radMemorySetCurrentAllocator( old );
                        }

                        p3d::context->GetLoadManager()->Load( request );
                    }
#if defined(RAD_PSP)
                    else if( mResources[i]->m_Status == RM_P3D_LOAD_IN_PROGRESS )
                    {
                        // A queued P3D-object load is still in progress (its
                        // entity hasn't appeared in the inventory yet). Normally
                        // we just wait (break). But if it has been in progress
                        // for many passes it most likely failed to load (missing
                        // file / parse error); skip it so the rest of the scene
                        // and the project load can finish rather than hang here.
                        //
                        // The room scene (camset.p3d, 2.26 MB / several thousand
                        // chunks) parses far slower than the small props, so the
                        // awaited entity (m_inventoryName) can take well over the
                        // old 600-pass window to appear — it was being skipped as
                        // a false "timeout", leaving the menu's 3D background black.
                        // Give big scenes room; the counter is just a hang backstop.
                        int inv = 0;
                        {
                            p3d::inventory->PushSection();
                            p3d::inventory->SelectSection( m_pInventorySection );
                            const char* want = ( mResources[i]->m_inventoryName == "" )
                                             ? mResources[i]->GetName()
                                             : (const char*)mResources[i]->m_inventoryName;
                            inv = ( p3d::find<tEntity>( want ) != NULL );
                            p3d::inventory->PopSection();
                        }
                        // A resource whose awaited inventory name is itself a ".p3d" file (the
                        // project's own frontend.p3d gets queued as a resource) can NEVER resolve
                        // to an entity of that name: the file streams its contents and registers
                        // entities under their own names, never "frontend.p3d". Waiting the full
                        // timeout on that phantom just stalls the whole project load. Give the
                        // async parse a short grace to add its child resources, then stop waiting.
                        {
                            const char* wnm = ( mResources[i]->m_inventoryName == "" )
                                            ? mResources[i]->GetName()
                                            : (const char*)mResources[i]->m_inventoryName;
                            size_t wl = wnm ? strlen( wnm ) : 0;
                            if( wl >= 4 && wnm[wl-4] == '.' &&
                                (wnm[wl-3]=='p'||wnm[wl-3]=='P') && wnm[wl-2]=='3' &&
                                (wnm[wl-1]=='d'||wnm[wl-1]=='D') &&
                                mResources[i]->m_pspLoadAttempts > 150 )
                            {
                                FILE* rf = fopen("ms0:/shar_rm.log","a");
                                if(rf){ fprintf(rf,"P3D skip (file-name resource, no such entity): %s\n", wnm); fclose(rf); }
                                mResources[i]->m_Status = RM_LOAD_COMPLETE;
                                continue;
                            }
                        }
                        if( (mResources[i]->m_pspLoadAttempts % 200) == 0 )
                        {
                            FILE* rf = fopen("ms0:/shar_rm.log","a");
                            if(rf){ fprintf(rf,"P3D in-progress pass %d: %s want='%s' found=%d\n",
                                    mResources[i]->m_pspLoadAttempts, mResources[i]->GetName(),
                                    (mResources[i]->m_inventoryName == "") ? mResources[i]->GetName()
                                        : (const char*)mResources[i]->m_inventoryName, inv); fclose(rf); }
                        }
                        if( ++mResources[i]->m_pspLoadAttempts > 6000 )
                        {
                            FILE* rf = fopen("ms0:/shar_rm.log","a");
                            if(rf){ fprintf(rf,"P3D load timed out, skipping: %s\n",
                                    mResources[i]->GetName()); fclose(rf); }
                            mResources[i]->m_Status = RM_LOAD_COMPLETE;
                            continue;
                        }
                    }
#endif

                    // We don't want to let any other files get queued up until this one is done
                    break;
                }
            }
            else
            {
                break;
            }
        }
    }

    ::radMemorySetCurrentAllocator( old );

#if defined(RAD_PSP)
    // The project-load-complete signal normally rides on the dummy
    // RT_PROJECT_LOAD_CALLBACK load's P3DCallback::Done() firing through
    // radLoad. In the standalone harness that callback bridge does not fire, so
    // detect completion here: once every resource is loaded (mCallbackID is
    // still set until then), fire the same two callbacks the dummy load would —
    // GetCallback()->OnResourceLoadComplete() (clears FeProject::mIsLoading so
    // the screen finally draws) and ProjectLoadComplete().
    if( m_pLoadingProject && mCallbackID != 0 )
    {
        bool allDone = true;
        int nComplete = 0, nTotal = 0;
        const char* pending = NULL;
        for( int k = 0; k < mResources.Size(); k++ )
        {
            if( mResources[k] )
            {
                nTotal++;
                if( mResources[k]->m_Status == RM_LOAD_COMPLETE ) nComplete++;
                else { allDone = false; if(!pending) pending = mResources[k]->GetName(); }
            }
        }
        {
            static int s_last = -1;
            if( nComplete != s_last )
            {
                s_last = nComplete;
                FILE* rf = fopen("ms0:/shar_rm.log","a");
                if(rf){ fprintf(rf,"resources %d/%d complete; pending='%s'\n",
                        nComplete, nTotal, pending ? pending : "(none)"); fclose(rf); }
            }
        }
        if( allDone )
        {
            { FILE* rf = fopen("ms0:/shar_rm.log","a"); if(rf){ fprintf(rf,"ALL DONE -> firing project complete\n"); fclose(rf);} }
            if( GetCallback() )
            {
                GetCallback()->OnResourceLoadComplete();
            }
            ProjectLoadComplete();   // clears mCallbackID -> fires once
        }
    }
#endif
}

//===========================================================================
// void DumpResources/DumpAliases/DumpPure3dAttributes
//===========================================================================
// Description: debugging functions to dump the contents of the lists
//
// Constraints:    requires the debug level to be set to LVL_DEBUG
//
// Parameters:    None
//
// Return:      None
//
//===========================================================================

void FeResourceManager::DumpResources()
{
    for( int i = 0; i < mResources.Size(); i++ )
    {
        if( mResources[i] )
        {
            //Scrooby::Log::Message( Scrooby::LVL_DEBUG, "Resource %d (type %d): %s ", i, mResources[i]->m_type, mResources[i]->GetName() );
        }
    }
}
void FeResourceManager::DumpAliases()
{
    for( int i = 0; i < mAliases.Size(); i++ )
    {
        if( mAliases[i] )
        {
            //Scrooby::Log::Message( Scrooby::LVL_DEBUG, "Alias %d : %d = %s", i, mAliases[i]->m_resourceIndex, mAliases[i]->GetName() );
        }
    }
}
void FeResourceManager::DumpPure3dAttributes()
{
    for( int i = 0; i < mP3DAttributes.Size(); i++ )
    {
        if( mP3DAttributes[i] )
        {
            //Scrooby::Log::Message( Scrooby::LVL_DEBUG, "P3D Attributes %d : %s, %s", (const char*)mP3DAttributes[i]->m_CameraName, (const char*)mP3DAttributes[i]->m_AnimName );
        }
    }
}

//===========================================================================
// Scrooby::ResourceManagerCallback* GetCallback
//===========================================================================
// Description: allows access to the callback function
//
// Constraints:    NONE
//
// Parameters:    NONE
//
// Return:      pointer to the current callback
//
//===========================================================================
Scrooby::ResourceManagerCallback* FeResourceManager::GetCallback() const
{
    return m_pResourceManagerCallback;
}

//===========================================================================
// tCamera* GetCamera
//===========================================================================
// Description: retrieves a camera, either by index or alias
//
// Constraints:    NONE
//
// Parameters:    alias - inventory name of the camera
//              index - index into the resources list
//
// Return:      pointer to a tCamera object
//
//===========================================================================
tCamera* FeResourceManager::GetCamera( const char* alias )
{
    return GetCamera( GetIndex( alias ) );
}
tCamera* FeResourceManager::GetCamera( int index )
{
    int i = 0;
    for( ; i < mP3DAttributes.Size(); i++ )
    {
        Pure3dAttributesEntry* attrib = mP3DAttributes[i];
        
        if( mP3DAttributes[i] && (mP3DAttributes[i]->m_resourceIndex == index) )
        {
            return p3d::find<tCamera>( mP3DAttributes[i]->m_CameraName );
        }
    }
    return NULL;
}

//===========================================================================
// int GetIndex
//===========================================================================
// Description: looks up the resource list index of an alias
//
// Constraints:    NONE
//
// Parameters:    alias - alias name of the object
//
// Return:      index into the resources list
//
//===========================================================================
int FeResourceManager::GetIndex( const tName& alias, bool searchBackwards )
{
    int size = mAliases.Size();
    if( searchBackwards )
    {
        int i;
        for( i = size - 1; i >= 0; --i )
        {
            AliasEntry* thisAlias = mAliases[ i ];
            if( ( thisAlias != NULL ) && ( alias == thisAlias->GetName() ) )
            {
                return thisAlias->m_resourceIndex;
            }
        }
    }
    else
    {
        int i;
        for( i = 0; i < size; ++i )
        {
            AliasEntry* thisAlias = mAliases[ i ];
            if( ( thisAlias != NULL ) && ( alias == thisAlias->GetName() ) )
            {
                return thisAlias->m_resourceIndex;
            }
        }
    }

    return -1;
}

//===========================================================================
// XLLanguage GetLocalizationLanguage() const
//===========================================================================
// Description: Retrieves the current language set in the ResourceManager
//
// Constraints:    None
//
// Parameters:    None
//
// Return:      The current language
//
//===========================================================================
Scrooby::XLLanguage FeResourceManager::GetLocalizationLanguage() const
{
    return m_eLang;
}

//===========================================================================
// tMultiController* GetMultiController
//===========================================================================
// Description: retrieves an animation controller, either by index or alias
//
// Constraints:    NONE
//
// Parameters:    alias - inventory name of the animation
//              index - index into the resources list
//
// Return:      pointer to a tMultiController object
//
//===========================================================================
tMultiController* FeResourceManager::GetMultiController( const char* alias )
{
    return GetMultiController( GetIndex( alias ) );
}
tMultiController* FeResourceManager::GetMultiController( int index )
{
    int i = 0;
    for( ; i < mP3DAttributes.Size(); i++ )
    {
        if( mP3DAttributes[i] && mP3DAttributes[i]->m_resourceIndex == index )
        {
            return p3d::find<tMultiController>( mP3DAttributes[i]->m_AnimName );
        }
    }
    return NULL;
}

//===========================================================================
// tEntity* GetResource
//===========================================================================
// Description: retrieves a resource, either by alias or index
//
// Constraints:    NONE
//
// Parameters:    alias - alias name of the resource
//              index - index into the resources list
//
// Return:      pointer to a tEntity object
//
//===========================================================================
tEntity* FeResourceManager::GetResource( const char* alias )
{
    return GetResource( GetIndex( alias ) );
}
tEntity* FeResourceManager::GetResource( int index )
{
    if( index != -1 &&
        mResources[index] != NULL )
    {
        return mResources[index]->theData;
    }
    else
    {
        return NULL;
    }
}

//===========================================================================
// bool IsAsyncLoading
//===========================================================================
// Description: returns the current loading method
//
// Constraints:    NONE
//
// Parameters:    None
//
// Return:      boolean : true if async loading, false otherwise
//
//===========================================================================
bool FeResourceManager::IsAsyncLoading()
{
    return m_bAsyncLoading;
}

//===========================================================================
// void LoadAll()
//===========================================================================
// Description: triggers loading of all the resoruces
//
// Constraints:    None
//
// Parameters:    NONE
//
// Return:      NONE
//
//===========================================================================
void FeResourceManager::LoadAll( Scrooby::ResourceManagerCallback* callback)
{
    //store the callback function
    SetCallback( callback );

    ContinueLoading();
}


//===========================================================================
// void LoadProject
//===========================================================================
// Description: triggers loading of all the resources associated with a project
//
// Constraints:    None
//
// Parameters:    project - a pointer to the project of interest
//              callback - the callback triggered when all loading is done
//
// Return:      NONE
//
//===========================================================================
void FeResourceManager::LoadProject(  Scrooby::Project* project, Scrooby::ResourceManagerCallback* callback )
{
    //store the callback function
    rAssert( callback );
    SetCallback( callback );

     FeProject* feProject = dynamic_cast<  FeProject* >( project );
    rAssert( feProject != NULL );

    int size = feProject->GetChildrenCount();
    int i;
    for( i = 0; i < size; i++ )
    {
        FeEntity* child = feProject->GetChildIndex( i );
        FeScreen* screen = dynamic_cast< FeScreen* >( child );
        rAssert( screen );
        //LoadChildren( screen, callback );
    }
    ContinueLoading();
}

//===========================================================================
// void ProjectLoadComplete
//===========================================================================
// Description: Signals the end of a project loading
//
// Constraints:    NONE
//
// Parameters:    NONE
//
// Return:      NONE
//
//===========================================================================
void FeResourceManager::ProjectLoadComplete()
{
    if( mCallbackID )
    {
        RemoveResource( mCallbackID );
        mCallbackID = 0;
    }

    FeApp::GetInstance()->OnProjectLoadComplete( m_pLoadingProject );
    //m_pLoadingProject = NULL;
}

//===========================================================================
// void RegisterProjectCallback
//===========================================================================
// Description: sets the callback to call when loading is done
//
// Constraints:    NONE
//
// Parameters:    callback - the new callback object
//
// Return:      none
//
//===========================================================================
void FeResourceManager::RegisterProjectCallback( Scrooby::LoadProjectCallback* callback )
{
    m_pLoadProjectCallback = callback;
}

//===========================================================================
// RegisterCementFile
//===========================================================================
// Description: Registers a cement file for resource loading
//
// Constraints:    None
//
// Parameters:    filename - the name of the cement file being registered
//
// Return:      None
//
//===========================================================================
void FeResourceManager::RegisterCementFile( const char* filename )
{
    //m_FileLoader->RegisterCementFile( filename );
}

//===========================================================================
// void RemoveResource
//===========================================================================
// Description: removes a resource from the resource list
//
// Constraints:    NONE
//
// Parameters:    resource - pointer to the resource to remove
//
// Return:      NONE
//
//===========================================================================
void FeResourceManager::RemoveResource( tEntity* resource, const char* inventorySection )
{
    for( int i = 0; i < mResources.Size(); i++ )
    {
        if( mResources[i] )
        {
            if( mResources[i]->theData == resource )
            {
                RemoveResource( i, inventorySection );
            }
        }
    }
}

void FeResourceManager::RemoveResource( int resourceID, const char* inventorySection )
{
    ResourceEntry* entry = mResources[resourceID];
    mResources[resourceID] = NULL;
    if( entry )
    {
        //Scrooby::Log::Message( Scrooby::LVL_INFO, "Unloading resource %s", entry->GetName() );
        if( entry->theData )
        {
            p3d::inventory->PushSection();
            if( inventorySection != NULL )
            {
                p3d::inventory->SelectSection( inventorySection );
            }
            else
            {
                p3d::inventory->SelectSection( m_pInventorySection );
            }
            p3d::inventory->Remove( entry->theData );
            entry->theData->Release();
            entry->theData = NULL;
            p3d::inventory->PopSection();
        }
        entry->ReleaseVerified();
        entry = NULL;
        mDeletedResources++;
    }
    RemoveAlias( resourceID );
    RemoveP3DAttributes( resourceID );
}

void FeResourceManager::RemoveAlias( int resourceID )
{
    int i = 0;
    for( i; i < mAliases.Size(); i++ )
    {
        if( mAliases[i] )
        {
            if( mAliases[i]->m_resourceIndex == resourceID )
            {
                delete mAliases[ i ];
                mAliases[ i ] = NULL;
                ++mDeletedAliases;
            }
        }
    }
}

void FeResourceManager::RemoveP3DAttributes( int resourceID )
{
    int i = 0;
    for( i; i < mP3DAttributes.Size(); i++ )
    {
        if( mP3DAttributes[i] )
        {
            if( mP3DAttributes[i]->m_resourceIndex == resourceID )
            {
                mP3DAttributes[i]->ReleaseVerified();
                mP3DAttributes[i] = NULL;
                mDeletedAttributes++;
            }
        }
    }
}

//===========================================================================
// void SetAsyncLoading
//===========================================================================
// Description: retrieves a resource, either by alias or index
//
// Constraints:    NONE
//
// Parameters:    async - sets whether or not to use async loading
//
// Return:      NONE
//
//===========================================================================
void FeResourceManager::SetAsyncLoading( bool async )
{
    m_bAsyncLoading = async;
}

//===========================================================================
// SetCallback
//===========================================================================
// Description: sets the new callback function
//
// Constraints:    NONE
//
// Parameters:    lang - the language to set all the translators to
// Return:      Nothing
//
//===========================================================================
void FeResourceManager::SetCallback( Scrooby::ResourceManagerCallback* callback )
{
    if( callback != NULL )
    {
        m_pResourceManagerCallback = callback;
    }
}

//===========================================================================
// SetInventorySection
//===========================================================================
// Description: sets inventory section to use for loading
//
// Constraints:    the inventory section must exist prior to loading
//
// Parameters:    name - Symbolic name for the section
//
// Return:      Nothing
//
//===========================================================================
void FeResourceManager::SetInventorySection( const char* name )
{
    rAssert( name != NULL );
    strcpy( m_pInventorySection, name );
}

//===========================================================================
// SetSecondaryInventorySection
//===========================================================================
// Description: sets the secondary inventory section to use for loading
//
// Constraints:    the inventory section must exist prior to loading
//
// Parameters:    name - Symbolic name for the section
//
// Return:      Nothing
//
//===========================================================================
void
FeResourceManager::SetSecondaryInventorySection( const char* name )
{
    if( name != NULL )
    {
        strcpy( m_pSecondaryInventorySection, name );
    }
    else
    {
        m_pSecondaryInventorySection[ 0 ] = '\0';
    }
}

//===========================================================================
// SetLocalizationLanguage
//===========================================================================
// Description: sets the language of all the localization text bibles in memory
//
// Constraints:    NONE
//
// Parameters:    lang - the language to set all the translators to
// Return:      Nothing
//
//===========================================================================
void FeResourceManager::SetLocalizationLanguage( const Scrooby::XLLanguage lang )
{
    int i = 0;
    if( m_eLang != lang )
    {
        m_eLang = lang;
        for( i; i < mResources.Size(); i++ )
        {
            if( mResources[i] )
            {
                FeTextBible* bible = dynamic_cast<FeTextBible*>( mResources[i]->theData );
                if( !bible )
                {
                    continue;
                }
                bible->SetLanguage( Scrooby::language_ID[ lang ] );
            }
        }
    }
}

//===========================================================================
// void UnloadAll();
//===========================================================================
// Description: triggers unloading of all resources
//
// Constraints:    NONE
//
// Parameters:    resourceId - the index of the resource that needs loading
//
// Return:      Nothing
//
//===========================================================================
void FeResourceManager::UnloadAll()
{
}

//===========================================================================
// UnloadProject
//===========================================================================
// Description: triggers unloading of all the resources associated with a project
//
// Constraints:    None
//
// Parameters:    project - a pointer to the project of interest
//
// Return:      NONE
//
//===========================================================================
void FeResourceManager::UnloadProject(  Scrooby::Project* project )
{
    FeProject* feProject = dynamic_cast<  FeProject* >( project );
    rAssert( feProject != NULL );

    int size = feProject->GetChildrenCount();
    int i;
    for( i = 0; i < size; i++ )
    {
        FeEntity* child = feProject->GetChildIndex( i );
        FeScreen* screen = dynamic_cast< FeScreen* >( child );
        rAssert  ( screen );
        //UnloadChildren( screen );
    }

    //
    // Wipe out all trace of resources from the resource manager
    //
    UnloadAll();
}

FeResourceManager::ResourceManagerEntryBase::ResourceManagerEntryBase()
:
    isThisCurrentlyBeingLoaded( false ),
    m_Status( RM_NOT_LOADED ),
    m_ScroobyName( NULL )
{
}
