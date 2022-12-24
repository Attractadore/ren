use std::env;
use std::path::PathBuf;

struct HeaderConfig {
    header: String,
    lib: String,
    rust_src: String,
    allow_function: String,
    block_file: String,
}

impl HeaderConfig {
    fn new(header: &str, lib: &str, rust_src: &str) -> Self {
        Self {
            header: header.to_string(),
            lib: lib.to_string(),
            rust_src: rust_src.to_string(),
            allow_function: String::new(),
            block_file: String::new(),
        }
    }

    fn set_allow_function(mut self, func: &str) -> Self {
        self.allow_function = String::from(func);
        self
    }

    fn set_block_file(mut self, func: &str) -> Self {
        self.block_file = String::from(func);
        self
    }
}

fn main() {
    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap();
    let target_env = env::var("CARGO_CFG_TARGET_ENV").unwrap();
    let profile = env::var("PROFILE").unwrap();
    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());

    let cmake_preset_os = if cfg!(unix) && target_os == "windows" {
        "linux-mingw"
    } else {
        &target_os
    };

    let cmake_preset_profile = if profile == "release" {
        "release"
    } else {
        "devel"
    };

    let dst = cmake::Config::new("..")
        .configure_arg("--preset")
        .configure_arg(format!("{cmake_preset_os}-{cmake_preset_profile}"))
        .configure_arg("-B")
        .configure_arg(format!("{}/build", out_dir.display()))
        .build();
    println!("cargo:rustc-link-search=native={}/lib", dst.display());

    let ren_h = "../include/ren/ren.h";
    let ren_lib = "ren";
    let ren_rs = "ren.rs";

    let ren_vk_h = "../include/ren/ren-vk.h";
    let ren_vk_lib = "ren-vk";
    let ren_vk_rs = "ren-vk.rs";

    let mut bindings_list = vec![
        HeaderConfig::new(ren_h, ren_lib, ren_rs).set_allow_function("ren_.*"),
        HeaderConfig::new(ren_vk_h, ren_vk_lib, ren_vk_rs)
            .set_allow_function("ren_vk_.*")
            .set_block_file(ren_h),
    ];

    if target_os == "windows" {
        let ren_dx12_h = "../include/ren/ren-dx12.h";
        let ren_dx12_lib = "ren-dx12";
        let ren_dx12_rs = "ren-dx12.rs";
        bindings_list.push(
            HeaderConfig::new(ren_dx12_h, ren_dx12_lib, ren_dx12_rs)
                .set_allow_function("ren_dx12_.*")
                .set_block_file(ren_h),
        );
        println!("cargo:rustc-link-lib=dylib=d3d12");
        println!("cargo:rustc-link-lib=dylib=dxgi");
        println!("cargo:rustc-link-lib=dylib=dxguid");
    }

    if target_env == "gnu" {
        println!("cargo:rustc-link-lib=dylib=stdc++");
    } else if target_env == "msvc" {
        if cfg!(release) {
            println!("cargo:rustc-link-lib=dylib=msvcrt");
        } else {
            println!("cargo:rustc-link-lib=dylib=msvcrtd");
        }
    }

    for bindings in bindings_list {
        println!("cargo:rerun-if-changed={}", bindings.header);
        println!("cargo:rustc-link-lib=static={}", bindings.lib);
        let mut bb = bindgen::Builder::default()
            .header(bindings.header)
            .default_enum_style(bindgen::EnumVariation::Rust {
                non_exhaustive: false,
            })
            .parse_callbacks(Box::new(bindgen::CargoCallbacks));
        if !bindings.allow_function.is_empty() {
            bb = bb.allowlist_function(bindings.allow_function.as_str())
        }
        if !bindings.block_file.is_empty() {
            bb = bb.blocklist_file(bindings.block_file.as_str());
        }
        bb.generate()
            .expect("Unable to generate bindings")
            .write_to_file(out_dir.join(bindings.rust_src))
            .expect("Couldn't write bindings!");
    }
}
