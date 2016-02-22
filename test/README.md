# htsnexus integration tests

These tests build and exercise the htsnexus indexer, server, and client together. First, the indexer is used to create a htsnexus database for a few data fles. Then the server is started on that database. The client is used to run various queries, and the results are then checked.

To run the tests, execute `./htsnexus_integration_tests.sh`. The [bash-tap](https://github.com/illusori/bash-tap) test cases are found in [htsnexus_integration.t](htsnexus_integration.t).
