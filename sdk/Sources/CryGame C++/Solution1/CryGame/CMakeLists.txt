# CryGame.dll - the Far Cry game module.

set(CRYGAME_MAIN
    AbiChecks.cpp
    CryGame.cpp
    StdAfx.cpp
)
source_group("Source Files" FILES ${CRYGAME_MAIN})

set(CRYGAME_UI
    IngameDialog.cpp
    IngameDialog.h
    ScriptObjectUI.cpp
    ScriptObjectUI.h
    UIButton.cpp
    UIButton.h
    UICheckBox.cpp
    UICheckBox.h
    UIComboBox.cpp
    UIComboBox.h
    UICVars.cpp
    UICVars.h
    UIDivX_Video.cpp
    UIDivX_Video.h
    UIEditBox.cpp
    UIEditBox.h
    UIHud.cpp
    UIHud.h
    UIListView.cpp
    UIListView.h
    UIMessages.h
    UIScreen.cpp
    UIScreen.h
    UIScrollBar.cpp
    UIScrollBar.h
    UIStatic.cpp
    UIStatic.h
    UISystem.cpp
    UISystem.h
    UIVideoPanel.cpp
    UIVideoPanel.h
    UIWidget.cpp
    UIWidget.h
)
source_group("Source Files\\UI" FILES ${CRYGAME_UI})

set(CRYGAME_SCRIPTOBJECTS
    ScriptObjectAdvCamSystem.cpp
    ScriptObjectAdvCamSystem.h
    ScriptObjectAI.cpp
    ScriptObjectAI.h
    ScriptObjectBoids.cpp
    ScriptObjectBoids.h
    ScriptObjectClient.cpp
    ScriptObjectClient.h
    ScriptObjectGame.cpp
    ScriptObjectGame.h
    ScriptObjectInput.cpp
    ScriptObjectInput.h
    ScriptObjectLanguage.cpp
    ScriptObjectLanguage.h
    ScriptObjectPlayer.cpp
    ScriptObjectPlayer.h
    ScriptObjectRenderer.cpp
    ScriptObjectRenderer.h
    ScriptObjectServer.cpp
    ScriptObjectServer.h
    ScriptObjectServerSlot.cpp
    ScriptObjectServerSlot.h
    ScriptObjectSpectator.cpp
    ScriptObjectSpectator.h
    ScriptObjectStream.cpp
    ScriptObjectStream.h
    ScriptObjectSynched2DTable.cpp
    ScriptObjectSynched2DTable.h
    ScriptObjectVehicle.cpp
    ScriptObjectVehicle.h
    ScriptObjectWeaponClass.cpp
    ScriptObjectWeaponClass.h
)
source_group("Source Files\\ScriptObjects" FILES ${CRYGAME_SCRIPTOBJECTS})

set(CRYGAME_GAMESERVER
    XServer.cpp
    XServer.h
    XServerRules.cpp
    XServerRules.h
    XServerSlot.cpp
    XServerSlot.h
    XSnapshot.cpp
    XSystemServer.h
)
source_group("Source Files\\GameServer" FILES ${CRYGAME_GAMESERVER})

set(CRYGAME_GAMESYSTEM
    IXSystem.h
    XSystemBase.cpp
    XSystemBase.h
    XSystemClient.cpp
    XSystemClient.h
    XSystemDummy.cpp
    XSystemDummy.h
    XSystemEntities.cpp
    XSystemEntities.h
    XSystemServer.cpp
)
source_group("Source Files\\GameSystem" FILES ${CRYGAME_GAMESYSTEM})

set(CRYGAME_WEAPONS
    FireType.h
    WeaponClass.cpp
    WeaponClass.h
    WeaponSystemEx.cpp
    WeaponSystemEx.h
)
source_group("Source Files\\Weapons" FILES ${CRYGAME_WEAPONS})

set(CRYGAME_GAMECLIENT
    XClient.cpp
    XClient.h
    XClientSnapshot.cpp
    XClientSnapshot.h
)
source_group("Source Files\\GameClient" FILES ${CRYGAME_GAMECLIENT})

set(CRYGAME_FLOCK
    BugsFlock.cpp
    BugsFlock.h
    Flock.cpp
    Flock.h
)
source_group("Source Files\\Flock" FILES ${CRYGAME_FLOCK})

set(CRYGAME_VEHICLE
    XplayerVehicle.cpp
    XVehicle.cpp
    XVehicle.h
    XVehicleProxy.cpp
    XVehicleProxy.h
    XVehicleProxyHeli.cpp
    XVehicleSystem.cpp
    XVehicleSystem.h
)
source_group("Source Files\\Vehicle" FILES ${CRYGAME_VEHICLE})

set(CRYGAME_TIMEDEMORECORDER
    TimeDemoRecorder.cpp
    TimeDemoRecorder.h
    XDemoMgr.cpp
    XDemoMgr.h
)
source_group("Source Files\\TimeDemoRecorder" FILES ${CRYGAME_TIMEDEMORECORDER})

set(CRYGAME_PLAYER
    AdvCamSystem.cpp
    AdvCamSystem.h
    PlayerSystem.h
    Spectator.cpp
    Spectator.h
    SynchedRandomSeed.cpp
    XPlayer.cpp
    XPlayer.h
    XPlayerCamera.cpp
    XPlayerLight.cpp
)
source_group("Source Files\\Player" FILES ${CRYGAME_PLAYER})

set(CRYGAME_AI
    AIHandler.cpp
    AIHandler.h
    XObjectProxy.cpp
    XObjectProxy.h
    XPuppetProxy.cpp
    XPuppetProxy.h
)
source_group("Source Files\\AI" FILES ${CRYGAME_AI})

set(CRYGAME_NETWORK
    BitStream_Base.cpp
    BitStream_Base.h
    BitStream_Compressed.cpp
    BitStream_Compressed.h
    NetEntityInfo.cpp
    NetEntityInfo.h
    SynchedRandomSeed.h
    XEntityProcessingCmd.cpp
    XEntityProcessingCmd.h
    XNetwork.cpp
    XNetwork.h
    XNetworkStats.cpp
    XNetworkStats.h
    XSnapshot.h
)
source_group("Source Files\\Network" FILES ${CRYGAME_NETWORK})

set(CRYGAME_GAMEFILES
    CMovieUser.h
    EntityClassRegistry.cpp
    EntityClassRegistry.h
    Game.cpp
    Game.h
    GameActionInput.cpp
    GameCallbacks.cpp
    GameClientServer.cpp
    GameDevMode.cpp
    GameEquipPacks.cpp
    GameEvents.cpp
    GameMemStats.cpp
    GameMisc.cpp
    GameMods.cpp
    GameMods.h
    GameMovieUser.cpp
    GameObject.h
    GameRadar.cpp
    GameShared.h
    GameTagPoints.cpp
    ScriptTimerMgr.cpp
    ScriptTimerMgr.h
    TagPoint.h
    XArea.cpp
    XArea.h
)
source_group("Source Files\\GameFiles" FILES ${CRYGAME_GAMEFILES})

set(CRYGAME_MATERIALS
    XSurfaceMgr.cpp
    XSurfaceMgr.h
)
source_group("Source Files\\Materials" FILES ${CRYGAME_MATERIALS})

set(CRYGAME_LOCALIZATION
    StringTableMgr.cpp
    StringTableMgr.h
)
source_group("Source Files\\Localization" FILES ${CRYGAME_LOCALIZATION})

set(CRYGAME_GAMESERIALIZATION
    GameLoading.cpp
)
source_group("Source Files\\GameSerialization" FILES ${CRYGAME_GAMESERIALIZATION})

set(CRYGAME_SCOREBOARD
    Synched2DTable.cpp
    Synched2DTable.h
)
source_group("Source Files\\Scoreboard" FILES ${CRYGAME_SCOREBOARD})

set(CRYGAME_HEADERS
    CryGame.h
    StdAfx.h
)
source_group("Header Files" FILES ${CRYGAME_HEADERS})


add_library(CryGame SHARED
    ${CRYGAME_MAIN}
    ${CRYGAME_UI}
    ${CRYGAME_SCRIPTOBJECTS}
    ${CRYGAME_GAMESERVER}
    ${CRYGAME_GAMESYSTEM}
    ${CRYGAME_WEAPONS}
    ${CRYGAME_GAMECLIENT}
    ${CRYGAME_FLOCK}
    ${CRYGAME_VEHICLE}
    ${CRYGAME_TIMEDEMORECORDER}
    ${CRYGAME_PLAYER}
    ${CRYGAME_AI}
    ${CRYGAME_NETWORK}
    ${CRYGAME_GAMEFILES}
    ${CRYGAME_MATERIALS}
    ${CRYGAME_LOCALIZATION}
    ${CRYGAME_GAMESERIALIZATION}
    ${CRYGAME_SCOREBOARD}
    ${CRYGAME_HEADERS}
)
set_target_properties(CryGame PROPERTIES FOLDER "Game")

if(CRYGAME_USE_STLPORT)
    # Linked first so that STLport precedes everything else on the include path.
    target_link_libraries(CryGame PRIVATE STLport)
endif()
target_link_libraries(CryGame PRIVATE CryCommon)

target_compile_definitions(CryGame PRIVATE
    _USRDLL
    CRYGAME_EXPORTS
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:_RELEASE>
)

target_precompile_headers(CryGame PRIVATE StdAfx.h)

target_compile_options(CryGame PRIVATE
    /W3
    # Global operator new/delete are replaced inline by CryMemoryManager.h.
    /wd4595
    # Keep VC7.1 semantics: every delete goes through the replaced unsized operator delete.
    /Zc:sizedDealloc-
)
if(NOT FARCRY_X64)
    # The retail binaries use x87 math; keep game-side float results identical for multiplayer.
    target_compile_options(CryGame PRIVATE /arch:IA32)
endif()

target_link_libraries(CryGame PRIVATE shlwapi wininet ws2_32)
# A map file next to the DLL helps with crash addresses from the retail engine.
target_link_options(CryGame PRIVATE /MAP)

if(CRYGAME_OUTPUT_DIR)
    # Release is linked without debug info (as in the original project), so it has no PDB.
    set(CRYGAME_DEPLOY_FILES
        "$<TARGET_FILE:CryGame>"
        "$<$<NOT:$<CONFIG:Release,MinSizeRel>>:$<TARGET_PDB_FILE:CryGame>>"
    )
    add_custom_command(TARGET CryGame POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CRYGAME_OUTPUT_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CRYGAME_DEPLOY_FILES}" "${CRYGAME_OUTPUT_DIR}"
        COMMENT "Copying CryGame.dll to ${CRYGAME_OUTPUT_DIR}"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )
endif()
