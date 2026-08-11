#include <Fluxion/Core/Plugin/Manager.h>

#include <Fluxion/Core/Startup/SubsystemRegistry.h>
#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/Foundation/Containers/DynamicArray.h>
#include <Fluxion/Foundation/Containers/HashMap.h>
#include <Fluxion/Foundation/Hashing.h>
#include <Fluxion/Foundation/Log.h>
#include <Fluxion/Platform/DynamicLibrary.h>
#include <Fluxion/Platform/File.h>

#include <pdjson.h>

#include <stdio.h>
#include <string.h>

typedef struct FluxionLoadedPlugin
{
    FluxionPluginDescriptor descriptor;
    FluxionDynamicLibrary library;
    FluxionPluginAPI api;
} FluxionLoadedPlugin;

static FluxionAllocator* s_allocator = NULL;
static FluxionDynamicArray s_loadedPlugins; // FluxionLoadedPlugin
static bool s_initialized = false;

void Fluxion_PluginManager_Init(FluxionAllocator* allocator)
{
    FLUXION_ASSERT_MSG(!s_initialized, "Fluxion_PluginManager_Init called twice without a Shutdown in between");
    s_allocator = allocator ? allocator : Fluxion_DefaultAllocator();
    Fluxion_DynamicArray_Init(&s_loadedPlugins, s_allocator, sizeof(FluxionLoadedPlugin));
    s_initialized = true;
}

void Fluxion_PluginManager_Shutdown(void)
{
    FLUXION_ASSERT_MSG(s_initialized, "Fluxion_PluginManager_Shutdown called before Init");

    for (usize i = s_loadedPlugins.count; i > 0; --i)
    {
        FluxionLoadedPlugin* plugin = (FluxionLoadedPlugin*)Fluxion_DynamicArray_At(&s_loadedPlugins, i - 1);

        FluxionPluginUnloadFn unload = (FluxionPluginUnloadFn)Fluxion_Platform_GetSymbol(&plugin->library, FLUXION_PLUGIN_UNLOAD_SYMBOL_NAME);
        if (unload)
        {
            unload(&plugin->api);
        }
        Fluxion_Platform_UnloadDynamicLibrary(&plugin->library);
    }

    Fluxion_DynamicArray_Destroy(&s_loadedPlugins);
    s_initialized = false;
}

usize Fluxion_PluginManager_GetLoadedCount(void)
{
    return s_loadedPlugins.count;
}

const FluxionPluginDescriptor* Fluxion_PluginManager_GetLoadedDescriptor(usize index)
{
    return &((FluxionLoadedPlugin*)Fluxion_DynamicArray_At(&s_loadedPlugins, index))->descriptor;
}

const FluxionPluginAPI* Fluxion_PluginManager_GetLoadedApi(usize index)
{
    return &((FluxionLoadedPlugin*)Fluxion_DynamicArray_At(&s_loadedPlugins, index))->api;
}

static void Fluxion_CopyBoundedString(char* dest, usize destSize, const char* src, usize srcLength)
{
    usize copyLength = srcLength < destSize - 1 ? srcLength : destSize - 1;
    memcpy(dest, src, copyLength);
    dest[copyLength] = '\0';
}

static void Fluxion_GetDirectory(const char* path, char* outBuffer, usize bufferSize)
{
    const char* lastSlash = strrchr(path, '/');
    const char* lastBackslash = strrchr(path, '\\');
    const char* last = lastSlash;
    if (lastBackslash && (!last || lastBackslash > last))
    {
        last = lastBackslash;
    }

    if (!last)
    {
        outBuffer[0] = '\0';
        return;
    }

    Fluxion_CopyBoundedString(outBuffer, bufferSize, path, (usize)(last - path));
}

static bool Fluxion_ReadEntireFile(FluxionAllocator* allocator, const char* path, char** outBuffer, usize* outSize)
{
    FluxionFile file;
    if (!Fluxion_Platform_FileOpen(&file, path, FLUXION_FILE_OPEN_READ))
    {
        return false;
    }

    i64 size = Fluxion_Platform_FileSize(&file);
    if (size < 0)
    {
        Fluxion_Platform_FileClose(&file);
        return false;
    }

    char* buffer = (char*)Fluxion_Allocator_Alloc(allocator, (usize)size + 1, FLUXION_DEFAULT_ALIGNMENT);
    usize bytesRead = Fluxion_Platform_FileRead(&file, buffer, (usize)size);
    Fluxion_Platform_FileClose(&file);

    if (bytesRead != (usize)size)
    {
        Fluxion_Allocator_Free(allocator, buffer, (usize)size + 1);
        return false;
    }

    buffer[size] = '\0';
    *outBuffer = buffer;
    *outSize = (usize)size;
    return true;
}

static bool Fluxion_ParsePluginTypeName(const char* name, usize length, FluxionPluginType* outType)
{
    static const struct { const char* name; FluxionPluginType type; } s_names[] =
    {
        { "Runtime",   FLUXION_PLUGIN_TYPE_RUNTIME },
        { "Renderer",  FLUXION_PLUGIN_TYPE_RENDERER },
        { "RHI",       FLUXION_PLUGIN_TYPE_RHI },
        { "Physics",   FLUXION_PLUGIN_TYPE_PHYSICS },
        { "Audio",     FLUXION_PLUGIN_TYPE_AUDIO },
        { "Importer",  FLUXION_PLUGIN_TYPE_IMPORTER },
        { "Editor",    FLUXION_PLUGIN_TYPE_EDITOR },
        { "Developer", FLUXION_PLUGIN_TYPE_DEVELOPER },
        { "Game",      FLUXION_PLUGIN_TYPE_GAME },
    };

    for (usize i = 0; i < FLUXION_ARRAY_COUNT(s_names); ++i)
    {
        usize nameLength = strlen(s_names[i].name);
        if (nameLength == length && memcmp(s_names[i].name, name, length) == 0)
        {
            *outType = s_names[i].type;
            return true;
        }
    }
    return false;
}

// `.plugin` file format (JSON):
//   { "name": "...", "library": "...", "version": 1, "type": "Runtime", "dependencies": ["..."] }
static bool Fluxion_ParsePluginDescriptor(const char* jsonText, usize jsonLength, FluxionPluginDescriptor* outDescriptor)
{
    memset(outDescriptor, 0, sizeof(*outDescriptor));

    json_stream json;
    json_open_buffer(&json, jsonText, jsonLength);

    bool ok = true;
    enum json_type type = json_next(&json);
    if (type != JSON_OBJECT)
    {
        ok = false;
    }

    while (ok && (type = json_next(&json)) == JSON_STRING)
    {
        // json_get_string's reported length includes pdjson's own
        // trailing NUL (it pushes '\0' through the same counted buffer
        // it counts real characters into) — one more than the actual
        // string content. None of our fields can legitimately contain an
        // embedded NUL, so strlen() on the guaranteed-terminated result
        // is simpler and correct, instead of trusting that count as-is.
        const char* key = json_get_string(&json, NULL);
        char keyBuffer[32];
        Fluxion_CopyBoundedString(keyBuffer, sizeof(keyBuffer), key, strlen(key));

        if (strcmp(keyBuffer, "name") == 0 || strcmp(keyBuffer, "library") == 0)
        {
            type = json_next(&json);
            if (type != JSON_STRING) { ok = false; break; }
            const char* value = json_get_string(&json, NULL);
            char* destination = (strcmp(keyBuffer, "name") == 0) ? outDescriptor->name : outDescriptor->library;
            Fluxion_CopyBoundedString(destination, FLUXION_PLUGIN_MAX_NAME_LENGTH + 1, value, strlen(value));
        }
        else if (strcmp(keyBuffer, "version") == 0)
        {
            type = json_next(&json);
            if (type != JSON_NUMBER) { ok = false; break; }
            outDescriptor->version = (u32)json_get_number(&json);
        }
        else if (strcmp(keyBuffer, "type") == 0)
        {
            type = json_next(&json);
            if (type != JSON_STRING) { ok = false; break; }
            const char* value = json_get_string(&json, NULL);
            if (!Fluxion_ParsePluginTypeName(value, strlen(value), &outDescriptor->type)) { ok = false; break; }
        }
        else if (strcmp(keyBuffer, "dependencies") == 0)
        {
            type = json_next(&json);
            if (type != JSON_ARRAY) { ok = false; break; }
            while ((type = json_next(&json)) == JSON_STRING)
            {
                if (outDescriptor->dependencyCount >= FLUXION_PLUGIN_MAX_DEPENDENCIES) { ok = false; break; }
                const char* value = json_get_string(&json, NULL);
                Fluxion_CopyBoundedString(
                    outDescriptor->dependencies[outDescriptor->dependencyCount],
                    FLUXION_PLUGIN_MAX_NAME_LENGTH + 1, value, strlen(value));
                outDescriptor->dependencyCount++;
            }
            if (ok && type != JSON_ARRAY_END) { ok = false; }
        }
        else
        {
            json_skip(&json);
        }
    }

    if (ok && type != JSON_OBJECT_END)
    {
        ok = false;
    }

    if (ok && (outDescriptor->name[0] == '\0' || outDescriptor->library[0] == '\0'))
    {
        ok = false;
    }

    json_close(&json);
    return ok;
}

bool Fluxion_PluginManager_LoadAll(const char* const* pluginFilePaths, usize count)
{
    FLUXION_ASSERT(s_initialized);

    if (count == 0)
    {
        return true;
    }

    FluxionDynamicArray descriptors;
    FluxionDynamicArray libraryPaths; // element size = 640 bytes, fixed-width path buffer
    Fluxion_DynamicArray_Init(&descriptors, s_allocator, sizeof(FluxionPluginDescriptor));
    Fluxion_DynamicArray_Init(&libraryPaths, s_allocator, 640);
    Fluxion_DynamicArray_Reserve(&descriptors, count);
    Fluxion_DynamicArray_Reserve(&libraryPaths, count);

    bool ok = true;

    // Read and parse every .plugin file, resolving each one's library path
    // (next to the .plugin file, OS-specific file name).
    for (usize i = 0; i < count && ok; ++i)
    {
        char* fileText = NULL;
        usize fileSize = 0;
        if (!Fluxion_ReadEntireFile(s_allocator, pluginFilePaths[i], &fileText, &fileSize))
        {
            FLUXION_LOG_ERROR("Plugin", "Failed to read plugin descriptor: %s", pluginFilePaths[i]);
            ok = false;
            break;
        }

        FluxionPluginDescriptor descriptor;
        bool parsed = Fluxion_ParsePluginDescriptor(fileText, fileSize, &descriptor);
        Fluxion_Allocator_Free(s_allocator, fileText, fileSize + 1);

        if (!parsed)
        {
            FLUXION_LOG_ERROR("Plugin", "Malformed plugin descriptor: %s", pluginFilePaths[i]);
            ok = false;
            break;
        }

        Fluxion_DynamicArray_Push(&descriptors, &descriptor);

        char directory[512];
        Fluxion_GetDirectory(pluginFilePaths[i], directory, sizeof(directory));

        char libraryFileName[128];
        Fluxion_Platform_GetDynamicLibraryFileName(descriptor.library, libraryFileName, sizeof(libraryFileName));

        char fullPath[640];
        if (directory[0] != '\0')
        {
            snprintf(fullPath, sizeof(fullPath), "%s/%s", directory, libraryFileName);
        }
        else
        {
            snprintf(fullPath, sizeof(fullPath), "%s", libraryFileName);
        }
        Fluxion_DynamicArray_Push(&libraryPaths, fullPath);
    }

    // Build a name -> index map and validate that every dependency resolves.
    FluxionHashMap nameToIndex;
    Fluxion_HashMap_Init(&nameToIndex, s_allocator, FLUXION_PLUGIN_MAX_NAME_LENGTH + 1, sizeof(usize), Fluxion_HashBytes64, Fluxion_BytesEqual);

    if (ok)
    {
        for (usize i = 0; i < descriptors.count; ++i)
        {
            FluxionPluginDescriptor* descriptor = (FluxionPluginDescriptor*)Fluxion_DynamicArray_At(&descriptors, i);
            Fluxion_HashMap_Set(&nameToIndex, descriptor->name, &i);
        }

        for (usize i = 0; i < descriptors.count && ok; ++i)
        {
            FluxionPluginDescriptor* descriptor = (FluxionPluginDescriptor*)Fluxion_DynamicArray_At(&descriptors, i);
            for (u32 d = 0; d < descriptor->dependencyCount; ++d)
            {
                if (!Fluxion_HashMap_Find(&nameToIndex, descriptor->dependencies[d]))
                {
                    FLUXION_LOG_ERROR("Plugin", "Plugin '%s' depends on unknown plugin '%s'", descriptor->name, descriptor->dependencies[d]);
                    ok = false;
                    break;
                }
            }
        }
    }

    // Topological sort (O(N^2) repeated-scan variant — N is always small
    // here, this runs once at startup, simplicity wins over building
    // proper adjacency lists for it).
    usize* order = NULL;
    bool* resolved = NULL;
    usize orderCount = 0;

    if (ok)
    {
        order = (usize*)Fluxion_Allocator_Alloc(s_allocator, descriptors.count * sizeof(usize), FLUXION_DEFAULT_ALIGNMENT);
        resolved = (bool*)Fluxion_Allocator_Alloc(s_allocator, descriptors.count * sizeof(bool), FLUXION_DEFAULT_ALIGNMENT);
        memset(resolved, 0, descriptors.count * sizeof(bool));

        while (ok && orderCount < descriptors.count)
        {
            usize progressBefore = orderCount;

            for (usize i = 0; i < descriptors.count; ++i)
            {
                if (resolved[i])
                {
                    continue;
                }

                FluxionPluginDescriptor* descriptor = (FluxionPluginDescriptor*)Fluxion_DynamicArray_At(&descriptors, i);
                bool ready = true;
                for (u32 d = 0; d < descriptor->dependencyCount; ++d)
                {
                    usize* depIndex = (usize*)Fluxion_HashMap_Find(&nameToIndex, descriptor->dependencies[d]);
                    FLUXION_ASSERT_MSG(depIndex != NULL, "dependency should already be validated above");
                    if (!resolved[*depIndex])
                    {
                        ready = false;
                        break;
                    }
                }

                if (ready)
                {
                    resolved[i] = true;
                    order[orderCount++] = i;
                }
            }

            if (orderCount == progressBefore)
            {
                FLUXION_LOG_ERROR("Plugin", "Circular plugin dependency detected among the remaining %zu plugin(s)", descriptors.count - orderCount);
                ok = false;
            }
        }
    }

    // Actually load, in dependency order. On any failure, unload
    // everything this call loaded so far (reverse order) — LoadAll never
    // leaves a partial result.
    FluxionDynamicArray newlyLoaded; // FluxionLoadedPlugin
    Fluxion_DynamicArray_Init(&newlyLoaded, s_allocator, sizeof(FluxionLoadedPlugin));

    if (ok)
    {
        FluxionPluginHostAPI host;
        host.apiVersion = FLUXION_PLUGIN_HOST_API_VERSION;
        host.defaultAllocator = Fluxion_DefaultAllocator();
        host.registerSubsystem = Fluxion_SubsystemRegistry_Register;

        for (usize k = 0; k < orderCount && ok; ++k)
        {
            usize i = order[k];
            FluxionPluginDescriptor* descriptor = (FluxionPluginDescriptor*)Fluxion_DynamicArray_At(&descriptors, i);
            const char* libraryPath = (const char*)Fluxion_DynamicArray_At(&libraryPaths, i);

            FluxionLoadedPlugin loaded;
            loaded.descriptor = *descriptor;
            loaded.api.userData = NULL;

            if (!Fluxion_Platform_LoadDynamicLibrary(&loaded.library, libraryPath))
            {
                FLUXION_LOG_ERROR("Plugin", "Failed to load plugin library '%s' for plugin '%s'", libraryPath, descriptor->name);
                ok = false;
                break;
            }

            FluxionPluginLoadFn load = (FluxionPluginLoadFn)Fluxion_Platform_GetSymbol(&loaded.library, FLUXION_PLUGIN_LOAD_SYMBOL_NAME);
            if (!load || !load(&host, &loaded.api))
            {
                FLUXION_LOG_ERROR("Plugin", "Plugin '%s' failed to load (missing or failing %s)", descriptor->name, FLUXION_PLUGIN_LOAD_SYMBOL_NAME);
                Fluxion_Platform_UnloadDynamicLibrary(&loaded.library);
                ok = false;
                break;
            }

            FLUXION_LOG_INFO("Plugin", "Loaded plugin '%s'", descriptor->name);
            Fluxion_DynamicArray_Push(&newlyLoaded, &loaded);
        }

        if (!ok)
        {
            for (usize i = newlyLoaded.count; i > 0; --i)
            {
                FluxionLoadedPlugin* plugin = (FluxionLoadedPlugin*)Fluxion_DynamicArray_At(&newlyLoaded, i - 1);
                FluxionPluginUnloadFn unload = (FluxionPluginUnloadFn)Fluxion_Platform_GetSymbol(&plugin->library, FLUXION_PLUGIN_UNLOAD_SYMBOL_NAME);
                if (unload)
                {
                    unload(&plugin->api);
                }
                Fluxion_Platform_UnloadDynamicLibrary(&plugin->library);
            }
        }
        else
        {
            for (usize i = 0; i < newlyLoaded.count; ++i)
            {
                Fluxion_DynamicArray_Push(&s_loadedPlugins, Fluxion_DynamicArray_At(&newlyLoaded, i));
            }
        }
    }

    Fluxion_DynamicArray_Destroy(&newlyLoaded);
    if (order) Fluxion_Allocator_Free(s_allocator, order, descriptors.count * sizeof(usize));
    if (resolved) Fluxion_Allocator_Free(s_allocator, resolved, descriptors.count * sizeof(bool));
    Fluxion_HashMap_Destroy(&nameToIndex);
    Fluxion_DynamicArray_Destroy(&descriptors);
    Fluxion_DynamicArray_Destroy(&libraryPaths);

    return ok;
}
