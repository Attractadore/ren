use std::env;
use std::path::PathBuf;
use std::process::Command;

fn main() {
    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap();
    let target_env = env::var("CARGO_CFG_TARGET_ENV").unwrap();
    let profile = env::var("PROFILE").unwrap();
    let root_dir = env::current_dir().unwrap().join("..");
    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());

    let cmake_preset = if cfg!(unix) && target_os == "windows" {
        format!("linux-mingw-{profile}")
    } else {
        profile.clone()
    };

    let cmake_build_type = &profile;

    let vcpkg_triplet = if target_os == "windows" {
        if target_env == "gnu" {
            "x64-mingw-static"
        } else {
            "x64-windows-static-md"
        }
    } else {
        "x64-linux-release"
    };

    let build_dir = root_dir.join("build").join(format!("rust-{cmake_preset}"));
    let install_dir = build_dir.join("install");
    let include_dir = root_dir.join("include");
    let lib_dir = install_dir.join("lib");

    let status = Command::new("cmake")
        .arg("--preset")
        .arg(&cmake_preset)
        .arg("-S")
        .arg(&root_dir)
        .arg("-B")
        .arg(&build_dir)
        .arg(format!("-DVCPKG_TARGET_TRIPLET={vcpkg_triplet}"))
        .status();

    if !status.ok().map_or(false, |s| s.success()) {
        panic!("Failed to run CMake configure step");
    }

    let status = Command::new("cmake")
        .arg("--build")
        .arg(&build_dir)
        .arg("--config")
        .arg(cmake_build_type)
        .status();

    if !status.ok().map_or(false, |s| s.success()) {
        panic!("Failed to run CMake build step");
    }

    let status = Command::new("cmake")
        .arg("--install")
        .arg(&build_dir)
        .arg("--config")
        .arg(cmake_build_type)
        .arg("--prefix")
        .arg(&install_dir)
        .status();

    if !status.ok().map_or(false, |s| s.success()) {
        panic!("Failed to run CMake install step");
    }

    let ren_h = "ren/ren.h";
    let ren_lib = "ren";

    let ren_vk_h = "ren/ren-vk.h";

    let ren_headers = [ren_h, ren_vk_h];

    let ren_ffi_h = out_dir.join("ren-ffi.h");
    let ren_ffi_rs = out_dir.join("ren-ffi.rs");
    std::fs::write(&ren_ffi_h, {
        let mut v = vec![String::from("#pragma once")];
        v.extend(
            ren_headers
                .iter()
                .map(|h| format!("#include <{0}>", include_dir.join(h).display())),
        );
        v.join("\n")
    })
    .expect("Failed to create ffi header");

    println!("cargo:rustc-link-search=native={}", lib_dir.display());

    let ren_static_libs = [ren_lib];

    let ren_dynamic_libs = if target_env == "gnu" {
        vec!["stdc++"]
    } else if target_env == "msvc" && cmake_build_type == "debug" {
        vec!["msvcrtd"]
    } else {
        vec![]
    };

    for lib in ren_static_libs {
        println!("cargo:rustc-link-lib=static={lib}");
    }
    for lib in ren_dynamic_libs {
        println!("cargo:rustc-link-lib=dylib={lib}");
    }

    bindgen::Builder::default()
        .header(ren_ffi_h.into_os_string().to_str().unwrap())
        .default_enum_style(bindgen::EnumVariation::NewType {
            is_bitfield: false,
            is_global: true,
        })
        .prepend_enum_name(false)
        .enable_function_attribute_detection()
        .allowlist_function("ren_.*")
        .parse_callbacks(Box::new(bindgen::CargoCallbacks))
        .clang_arg("-isystem")
        .clang_arg({
            let vcpkg_dir = build_dir.join("vcpkg_installed");
            vcpkg_dir
                .join(vcpkg_triplet)
                .join("include")
                .into_os_string()
                .into_string()
                .unwrap()
        })
        .generate()
        .expect("Unable to generate bindings")
        .write_to_file(ren_ffi_rs)
        .expect("Couldn't write bindings!");
}
