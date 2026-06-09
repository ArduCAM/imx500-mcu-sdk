# flatcc metadata parser test

This test parses `test_metadata_outputtensor_1.h` with the generated
`apParams_c` flatcc reader/verifier headers and prints the IMX500 metadata
header, FlatBuffer networks, tensors, dimensions, and estimated output payload
size.

Build and run:

```sh
cmake -S tools/tests/flatcc_metadata_parser -B /tmp/flatcc_metadata_test
cmake --build /tmp/flatcc_metadata_test
/tmp/flatcc_metadata_test/parse_apparams_metadata_flatcc
```
