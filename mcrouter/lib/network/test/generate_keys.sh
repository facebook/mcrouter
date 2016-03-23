#!/bin/bash

# we assume the generate_key-inl.sh is in the same dir as this script
ROOT=$(dirname "$0")
. "$ROOT/generate_key-inl.sh"

generateCert test Asox 127.0.0.1 ::1
generateCert broken Asox 0.0.0.0 ::0

# Clean up serial number
rm "${CA_CERT_SRL}"
