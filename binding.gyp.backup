{
  "targets": [
    {
      "target_name": "opcda",
      "sources": [ "src/opcda.cpp" ],
      "include_dirs": [
        "include",
        "<!@(node -p \"require('node-addon-api').include\")",
        "<!@(node -p \"require('node-addon-api').include_dir\")",
        "<!(node -p \"process.env.npm_config_nodedir + '/include/node'\")"
      ],
      "dependencies": [
        "<!(node -p \"require('node-addon-api').gyp\")"
      ],
      "cflags_cc": [ "-std=c++17" ],
      "conditions": [
        ['OS=="win"', {
          "msvs_settings": {
            "VCCLCompilerTool": {
              "ExceptionHandling": "2",
              "DisableSpecificWarnings": [ "4530", "4506", "4996", "6386" ],
              "AdditionalOptions": [ "/std:c++17", "/EHsc" ]
            },
            "VCLinkerTool": {
              "AdditionalOptions": [ "/HIGHENTROPYVA:NO" ],
              "AdditionalDependencies": [ 
                "OPCClientToolKit64.lib",
                "ole32.lib",
                "oleaut32.lib",
                "rpcrt4.lib"
              ],
              "AdditionalLibraryDirectories": [ "lib/x64" ]
            }
          },
          "defines": [ "_CRT_SECURE_NO_WARNINGS" ]
        }]
      ],
      "libraries": [ "lib/x64/OPCClientToolKit64.lib" ]
    }
  ]
}