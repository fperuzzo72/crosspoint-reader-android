plugins {
  id("com.android.application")
  id("org.jetbrains.kotlin.android")
}

android {
  namespace = "org.crosspoint.hibreak"
  compileSdk = 35
  ndkVersion = "28.0.12433566"

  defaultConfig {
    applicationId = "org.crosspoint.hibreak"
    // O aparelho roda Android 14 (SDK 34). O minimo fica em 28 porque e o que
    // o NDK build usa e nao ha razao para exigir mais: nada aqui depende de
    // API nova. Se um dia depender, isto sobe junto com o ANDROID_PLATFORM.
    minSdk = 28
    targetSdk = 35
    versionCode = 1
    versionName = "hibreak-dev"

    ndk {
      abiFilters += "arm64-v8a"
    }

    externalNativeBuild {
      cmake {
        // c++_static: um .so so no APK, sem depender de libc++_shared estar
        // presente. O binario ja e grande e a STL estatica e a diferenca entre
        // funcionar e falhar no carregamento num aparelho de fabricante.
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
