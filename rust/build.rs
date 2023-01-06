use std::env;
use std::path::PathBuf;
use std::process::Command;

fn main() {
    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap();
    let target_env = env::var("CARGO_CFG_TARGET_ENV").unwrap();
    let profile = env::var("PROFILE").unwrap();
    let root_dir = env::current_dir().unwrap().join("..");
    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());

    let cmake_preset_os = if cfg!(unix) && target_os == "windows" {
        "linux-mingw"
    } else {
        &target_os
    };

    let (cmake_preset_profile, cmake_build_type) = if profile == "release" {
        ("release", "release")
    } else {
        ("devel", "debug")
    };

    let cmake_preset = format!("{cmake_preset_os}-{cmake_preset_profile}");
    let rust_preset = format!("rust-{cmake_preset}");

    let build_dir = root_dir.join("build").join(rust_preset);
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
    let ren_vk_lib = "ren-vk";

    let ren_dx12_h = "ren/ren-dx12.h";
    let ren_dx12_lib = "ren-dx12";

    let (ren_headers, ren_static_libs, mut ren_dynamic_libs) = if target_os == "windows" {
        (
            vec![ren_h, ren_vk_h, ren_dx12_h],
            vec![ren_lib, ren_vk_lib, ren_dx12_lib],
            vec!["d3d12", "dxgi", "dxguid"],
        )
    } else {
        (vec![ren_h, ren_vk_h], vec![ren_lib, ren_vk_lib], vec![])
    };

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

    if target_env == "gnu" {
        ren_dynamic_libs.push("stdc++");
    } else if target_env == "msvc" && cmake_build_type == "debug" {
        ren_dynamic_libs.push("msvcrtd");
    }

    for lib in ren_static_libs {
        println!("cargo:rustc-link-lib=static={lib}");
    }
    for lib in ren_dynamic_libs {
        println!("cargo:rustc-link-lib=dylib={lib}");
    }

    bindgen::Builder::default()
        .header(ren_ffi_h.into_os_string().to_str().unwrap())
        .default_enum_style(bindgen::EnumVariation::Rust {
            non_exhaustive: false,
        })
        .allowlist_function("ren_.*")
        .parse_callbacks(Box::new(bindgen::CargoCallbacks))
        .clang_arg("-isystem")
        .clang_arg({
            let vcpkg_dir = build_dir.join("vcpkg_installed");
            vcpkg_dir
                .join(
                    std::fs::read_to_string(vcpkg_dir.join("target_triplet"))
                        .unwrap()
                        .trim_end(),
                )
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
