engine_add_test(
    NAME AssetsTests
    SOURCES
        main.cpp
        Test_VirtualFileSystem.cpp
        Test_AssetTypes.cpp
        Test_AssetDatabase.cpp
        Test_AssetLoading.cpp
        Test_Package.cpp
        Test_AssetImport.cpp
        Test_AssetReload.cpp
    DEPENDENCIES
        Fluxion::Assets
        # These checks put files on disk and read the finished package
        # back as bytes, and they do it through the engine's own file
        # interface rather than the C library's -- the same way the thing
        # they are testing does.
        Fluxion::Platform
        # The import runtime hands its work to the job system.
        Fluxion::Core
    NO_EXCEPTIONS
    NO_RTTI
)

engine_add_header_selfcontained_test(
    MODULE Assets
    HEADERS
        Fluxion/Assets/AssetTypeId.h
        Fluxion/Assets/AssetType.h
        Fluxion/Assets/AssetRef.h
        Fluxion/Assets/AssetDatabase.h
        Fluxion/Assets/AssetService.h
        Fluxion/Assets/AssetSystem.h
        Fluxion/Assets/AssetImport.h
        Fluxion/Assets/Assets.h
        Fluxion/Assets/Package.h
        Fluxion/Assets/VirtualFileSystem.h
        Fluxion/Assets/Assets.hpp
)
