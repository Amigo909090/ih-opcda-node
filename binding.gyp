{
  "targets": [
    {
      "target_name": "opcda",
      "sources": [ "src/opcda.cpp" ],      
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "<(module_root_dir)/include",        
        "src"
      ],
      "defines": ["NAPI_DISABLE_CPP_EXCEPTIONS", "_CRT_SECURE_NO_WARNINGS"],
      "cflags": ["-Wall", "-Wno-unused-parameter"],
      "cflags_cc": ["-Wall", "-Wno-unused-parameter", "-std=c++17", "-fexceptions"],
      "conditions": [
        ["OS=='win' and target_arch=='x64'", {          
          "msvs_settings": {
            "VCCLCompilerTool": {
              "ExceptionHandling": 1,
              "DisableSpecificWarnings": [ "4530", "4506", "4996", "6386" ], 
              "RuntimeLibrary": "0",            
              "AdditionalOptions": ["/std:c++17", "/EHa"]
            }
          },
          "libraries": [
            "<(module_root_dir)/lib/x64/OPCClientToolKit64.lib",
            "ole32.lib",            
            "oleaut32.lib",
            "rpcrt4.lib",
            "ws2_32.lib",
            "advapi32.lib",
            "atls.lib" 
          ]
        }]
      ]
    }
  ]
}