plugins {
  id("com.android.application")
  id("org.jetbrains.kotlin.android")
}

android {
  namespace = "org.crosspoint.hibreak"
  // Otherwise the output is "app-debug.apk", which says nothing sitting in a
  // phone's download folder next to every other app-debug.apk. These are
  // installed by hand.
  base.archivesName = "crosspoint-hibreak"
  compileSdk = 35
  ndkVersion = "30.0.16248370"

  defaultConfig {
    applicationId = "org.crosspoint.hibreak"
    // The device runs Android 14 (SDK 34). The minimum stays at 28 because that
    // is what the NDK build uses and there is no reason to demand more: nothing
    // here depends on a newer API. If something ever does, this rises together
    // with ANDROID_PLATFORM.
    minSdk = 28
    targetSdk = 35
    versionCode = 1
    versionName = "hibreak-dev"

    ndk {
      abiFilters += "arm64-v8a"
    }

    externalNativeBuild {
      cmake {
        // c++_static: one .so in the APK, with no dependency on libc++_shared
        // being present. The binary is large already, and a static STL is the
        // difference between loading and failing to load on a vendor device.
        arguments += listOf("-DANDROID_STL=c++_static")
        cppFlags += "-std=c++20"
      }
    }
  }

  externalNativeBuild {
    cmake {
      path = file("../cmake/android/CMakeLists.txt")
      version = "3.22.1+"
    }
  }

  buildTypes {
    release {
      isMinifyEnabled = false
      // O .so sem strip tem 41MB e com strip 7,9MB. O Gradle faz isso sozinho
      // no release; no debug fica inteiro, que e o que se quer para depurar.
      proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
    }
  }

  compileOptions {
    sourceCompatibility = JavaVersion.VERSION_17
    targetCompatibility = JavaVersion.VERSION_17
  }
  kotlinOptions {
    jvmTarget = "17"
  }
}

dependencies {
  implementation("androidx.core:core-ktx:1.15.0")
  implementation("androidx.activity:activity:1.9.3")
}
