# FlatBuffers metadata parser test

This sibling project parses `../test_metadata_outputtensor_1.h` with the
standard FlatBuffers C++ generated reader in `ApParams.h`.

Build and run:

```sh
cmake -S tools/tests/flatbuffers_metadata_parser -B /tmp/flatbuffers_metadata_test
cmake --build /tmp/flatbuffers_metadata_test
/tmp/flatbuffers_metadata_test/parse_apparams_metadata_flatbuffers
```

Sibling parser projects:

- `../flatcc_metadata_parser`: flatcc C reader/verifier.
- `.`: standard FlatBuffers C++ reader/verifier.
