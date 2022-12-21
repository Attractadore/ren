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
    println!(
        "cargo:rustc-link-search={}/lib",
        cmake::build("..").display()
    );

    let ren_h = "../include/ren/ren.h";
    let ren_lib = "ren";
    let ren_rs = "ren.rs";

    let ren_vk_h = "../include/ren/ren-vk.h";
    let ren_vk_lib = "ren-vk";
    let ren_vk_rs = "ren-vk.rs";

    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    let bindings_list = [
        HeaderConfig::new(ren_h, ren_lib, ren_rs).set_allow_function("ren_.*"),
        HeaderConfig::new(ren_vk_h, ren_vk_lib, ren_vk_rs)
            .set_allow_function("ren_vk_.*")
            .set_block_file(ren_h),
    ];

    for bindings in bindings_list {
        println!("cargo:rerun-if-changed={}", bindings.header);
        println!("cargo:rustc-link-lib={}", bindings.lib);
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
            .write_to_file(out_path.join(bindings.rust_src))
            .expect("Couldn't write bindings!");
    }
}
