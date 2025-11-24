add_rules("mode.debug", "mode.release")

-- Use modern C++ 20
set_languages("cxx20")

-- Spinnaker SDK detection function (platform-agnostic, checks all paths)
function detect_spinnaker_sdk()
    -- Windows paths
    local win_include = "C:/Program Files/Teledyne/Spinnaker/include"
    local win_lib = "C:/Program Files/Teledyne/Spinnaker/lib64/vs2015/Spinnaker_v140.lib"
    if os.isdir(win_include) and os.isfile(win_lib) then
        return true, win_include, "C:/Program Files/Teledyne/Spinnaker/lib64/vs2015", {"Spinnaker_v140"}, "windows"
    end

    -- Linux paths
    local linux_include = "/opt/spinnaker/include"
    local linux_lib = "/opt/spinnaker/lib/libSpinnaker.so"
    if os.isdir(linux_include) and os.isfile(linux_lib) then
        return true, linux_include, "/opt/spinnaker/lib", {"Spinnaker", "SpinVideo"}, "linux"
    end

    -- macOS paths (standard installation)
    local macos_include = "/usr/local/include/spinnaker"
    local macos_lib = "/usr/local/lib/libSpinnaker.dylib"
    if os.isdir(macos_include) and os.isfile(macos_lib) then
        return true, macos_include, "/usr/local/lib", {"Spinnaker"}, "macosx"
    end

    -- macOS Framework installation
    local framework_path = "/Library/Frameworks/Spinnaker.framework"
    if os.isdir(framework_path) then
        return true, path.join(framework_path, "Headers"), path.join(framework_path, "Libraries"), {"Spinnaker"}, "macosx"
    end

    return false, nil, nil, nil, nil
end

-- Auto-detect Spinnaker SDK availability
local spinnaker_detected, spinnaker_include, spinnaker_libdir, spinnaker_libs, spinnaker_plat = detect_spinnaker_sdk()

-- Optional Spinnaker SDK support (for FLIR cameras like Chameleon3)
option("spinnaker")
    set_default(spinnaker_detected)
    set_showmenu(true)
    set_description("Enable FLIR Spinnaker SDK camera support (auto-detected)")

    after_check(function(option)
        if option:enabled() then
            if spinnaker_detected then
                print("Spinnaker SDK detected at " .. spinnaker_include .. " - enabled")
            else
                print("Spinnaker SDK force-enabled via --spinnaker=y")
            end
        else
            if spinnaker_detected then
                print("Spinnaker SDK found but disabled via --spinnaker=n")
            else
                print("Spinnaker SDK not found - building without Spinnaker support (use --spinnaker=y to force enable)")
            end
        end
    end)
option_end()

add_requireconfs("trantor", {version = "1.5.24", override = true})
add_requireconfs("drogon.trantor", {version = "1.5.24", override = true})
add_requires("opencv 4.12.0", {system = false})

add_requires(
    "trantor",
    "drogon",
    "opencv",
    "onnxruntime",
    "nlohmann_json",
    "sqlitecpp",
    "spdlog",
    "eigen",
    "librealsense"
)

-- AprilTag3 static library (built from source)
target("apriltag")
    set_kind("static")
    set_languages("c11")
    add_defines("_GNU_SOURCE") -- ensure POSIX/GNU extensions (strdup, random, clockid_t, etc.)

    -- Add source files (exclude Python wrapper)
    add_files("third_party/apriltag/*.c|apriltag_pywrap.c")
    add_files("third_party/apriltag/common/*.c")

    -- Include directories
    add_includedirs("third_party/apriltag", {public = true})
    add_includedirs("third_party/apriltag/common", {public = true})

    -- Windows-specific defines
    if is_plat("windows") then
        add_defines("_USE_MATH_DEFINES")
        add_defines("_CRT_SECURE_NO_WARNINGS")
    end

-- Main backend target
target("backend")
    set_kind("binary")
    add_files("src/**.cpp")
    add_headerfiles("src/**.hpp")
    add_includedirs("src")
    add_includedirs("third_party/cpp-mjpeg-streamer/include")

    -- Link against apriltag static library
    add_deps("apriltag")

    add_packages(
        "drogon",
        "opencv",
        "onnxruntime",
        "nlohmann_json",
        "sqlitecpp",
        "spdlog",
        "eigen"
    )

    -- Windows-specific system libraries
    if is_plat("windows") then
        add_defines("_WIN32_WINNT=0x0601")
        add_syslinks("shell32", "ole32")
    end

    -- Build dependencies and configure includes/links
    on_load(function (target)
        local backend_dir = os.scriptdir()

        -- NetworkTables (allwpilib) paths
        local allwpilib_dir = path.join(backend_dir, "third_party", "allwpilib")
        local ntcore_include_dir = path.join(allwpilib_dir, "ntcore", "src", "main", "native", "include")
        local ntcore_generated_dir = path.join(allwpilib_dir, "ntcore", "src", "generated", "main", "native", "include")
        local wpiutil_include_dir = path.join(allwpilib_dir, "wpiutil", "src", "main", "native", "include")
        local allwpilib_build_dir = path.join(allwpilib_dir, "build-cmake")

        -- =======================================================================
        -- Check and build NetworkTables (allwpilib)
        -- =======================================================================

        if not os.isdir(allwpilib_dir) then
            raise([[
[ERROR] NetworkTables (allwpilib) submodule not found!

Expected location: ]] .. allwpilib_dir .. [[

Please initialize the git submodule:
    git submodule update --init --recursive third_party/allwpilib
]])
        end

        if not os.isdir(ntcore_include_dir) then
            raise([[
[ERROR] NetworkTables source headers not found!

Expected include directory: ]] .. ntcore_include_dir .. [[

Please ensure the allwpilib submodule is properly initialized.
]])
        end

        if not os.isdir(allwpilib_build_dir) then
            print("Building allwpilib (ntcore, wpiutil)...")

            -- Configure with CMake using Visual Studio generator
            print("Configuring allwpilib with CMake...")
            os.mkdir(allwpilib_build_dir)
            local ok = os.execv("cmake", {
                "-S", allwpilib_dir,
                "-B", allwpilib_build_dir,
                "-G", "Visual Studio 18 2026",
                "-A", "x64",
                "-DWITH_JAVA=OFF",
                "-DWITH_TESTS=OFF",
                "-DWITH_SIMULATION_MODULES=OFF",
                "-DWITH_PROTOBUF=OFF",
                "-DWITH_WPILIB=OFF",
                "-DWITH_GUI=OFF",
                "-DWITH_CSCORE=OFF",
                "-DWITH_BENCHMARK=OFF"
            })
            if ok ~= 0 then
                raise("Failed to configure allwpilib with CMake")
            end

            -- Build ntcore and wpiutil
            print("Building ntcore and wpiutil...")
            ok = os.execv("cmake", {
                "--build", allwpilib_build_dir,
                "--config", "Release",
                "--target", "ntcore", "wpiutil",
                "--parallel"
            })
            if ok ~= 0 then
                raise("Failed to build allwpilib")
            end

            print("allwpilib built successfully!")
        end

        -- =======================================================================
        -- Configure NetworkTables includes and links
        -- =======================================================================

        target:add("includedirs", ntcore_include_dir)
        target:add("includedirs", ntcore_generated_dir)
        target:add("includedirs", wpiutil_include_dir)

        -- WPILib thirdparty dependencies
        local wpiutil_thirdparty = path.join(allwpilib_dir, "wpiutil", "src", "main", "native", "thirdparty")
        target:add("includedirs", path.join(wpiutil_thirdparty, "nanopb", "include"))
        target:add("includedirs", path.join(wpiutil_thirdparty, "llvm", "include"))
        target:add("includedirs", path.join(wpiutil_thirdparty, "fmtlib", "include"))
        target:add("includedirs", path.join(wpiutil_thirdparty, "expected", "include"))
        target:add("includedirs", path.join(wpiutil_thirdparty, "json", "include"))
        target:add("includedirs", path.join(wpiutil_thirdparty, "mpack", "include"))
        target:add("includedirs", path.join(wpiutil_thirdparty, "sigslot", "include"))

        if is_plat("windows") then
            target:add("linkdirs", path.join(allwpilib_build_dir, "lib", "Release"))
            target:add("links", "ntcore", "wpinet", "wpiutil")
            target:add("syslinks", "ws2_32", "iphlpapi")
        else
            target:add("linkdirs", path.join(allwpilib_build_dir, "lib"))
            target:add("links", "ntcore", "wpinet", "wpiutil", "pthread", "dl")
        end
    end)

    -- Copy DLLs to output directory after build (Windows only)
    after_build(function (target)
        local backend_dir = os.scriptdir()
        local target_dir = target:targetdir()

        -- Copy data directory
        local data_src = path.join(backend_dir, "data")
        if os.isdir(data_src) then
            os.cp(data_src, target_dir)
            print("Copied data directory to " .. target_dir)
        end

        if is_plat("windows") then
            local allwpilib_bin_dir = path.join(backend_dir, "third_party", "allwpilib", "build-cmake", "bin", "Release")

            -- List of DLLs to copy
            local dlls = {"ntcore.dll", "wpinet.dll", "wpiutil.dll"}

            for _, dll in ipairs(dlls) do
                local src = path.join(allwpilib_bin_dir, dll)
                local dst = path.join(target_dir, dll)
                if os.isfile(src) then
                    os.cp(src, dst)
                    print("Copied " .. dll .. " to " .. target_dir)
                end
            end
        end
    end)

    -- RealSense support
    add_packages("librealsense")

    -- Optional Spinnaker SDK support (for FLIR cameras)
    -- Uses delay-loaded DLLs on Windows for graceful degradation when SDK is not installed
    if has_config("spinnaker") then
        add_defines("VISION_WITH_SPINNAKER")
        if spinnaker_detected and spinnaker_include and spinnaker_libdir and spinnaker_libs then
            -- Use auto-detected paths
            add_includedirs(spinnaker_include)
            add_linkdirs(spinnaker_libdir)
            for _, lib in ipairs(spinnaker_libs) do
                add_links(lib)
            end
            -- Enable delay-load for graceful degradation on Windows
            if is_plat("windows") then
                add_ldflags("/DELAYLOAD:Spinnaker_v140.dll", {force = true})
                add_links("delayimp")
            end
        elseif is_plat("windows") then
            -- Fallback: FLIR Spinnaker SDK default installation paths for Windows
            add_includedirs("C:/Program Files/Teledyne/Spinnaker/include")
            add_linkdirs("C:/Program Files/Teledyne/Spinnaker/lib64/vs2015")
            add_links("Spinnaker_v140")
            -- Enable delay-load for graceful degradation
            add_ldflags("/DELAYLOAD:Spinnaker_v140.dll", {force = true})
            add_links("delayimp")
        elseif is_plat("linux") then
            -- Fallback: Linux Spinnaker SDK paths
            add_includedirs("/opt/spinnaker/include")
            add_linkdirs("/opt/spinnaker/lib")
            add_links("Spinnaker", "SpinVideo")
        elseif is_plat("macosx") then
            -- Fallback: macOS Spinnaker SDK paths
            add_includedirs("/usr/local/include/spinnaker")
            add_linkdirs("/usr/local/lib")
            add_links("Spinnaker")
        end
    end
