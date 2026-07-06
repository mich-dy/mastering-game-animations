plugins {
    id("com.android.application")
}

android {
    namespace = "com.michdy.masteringanimations"
    compileSdk = 35
    ndkVersion = "28.0.12674087"

    defaultConfig {
        applicationId = "com.michdy.masteringanimations"
        minSdk = 34
        targetSdk = 35
        versionCode = 1
        versionName = "1.0"

        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DANDROID_STL=c++_shared",
                    "-DANDROID_PLATFORM=android-34",
                    "-DANDROID_ARM_NEON=TRUE",
                    "-DCMAKE_POSITION_INDEPENDENT_CODE=ON"
                )
                cppFlags += listOf("-std=c++20", "-frtti", "-fexceptions")
                targets("Main")
            }
        }

        ndk {
            abiFilters += listOf("arm64-v8a")
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
        debug {
            isJniDebuggable = true
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../../CMakeLists.txt")
            version = "3.31.1"
        }
    }

    packaging {
        jniLibs {
            useLegacyPackaging = true
        }
    }

    sourceSets {
        getByName("main") {
            assets.srcDir("src/main/assets")
        }
    }
}

val prepareGameAssets = tasks.register<Copy>("prepareGameAssets") {
    from("../../shader") {
        into("shader")
    }
    from("../../textures") {
        into("textures")
    }
    from("../../assets") {
        into("game_assets")
    }
    from("../../config") {
        into("config")
    }
    from("../../controls.txt") {
        into(".")
    }
    into(layout.projectDirectory.dir("src/main/assets"))
}

afterEvaluate {
    tasks.findByName("buildCMakeDebug[arm64-v8a][Main]")?.let { cmakeTask ->
        prepareGameAssets.configure { dependsOn(cmakeTask) }
    }
    tasks.findByName("buildCMakeRelWithDebInfo[arm64-v8a][Main]")?.let { cmakeTask ->
        prepareGameAssets.configure { dependsOn(cmakeTask) }
    }
    tasks.findByName("mergeDebugAssets")?.dependsOn(prepareGameAssets)
    tasks.findByName("mergeReleaseAssets")?.dependsOn(prepareGameAssets)
}

dependencies {}