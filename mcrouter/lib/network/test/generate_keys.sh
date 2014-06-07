#!/bin/bash

# This script exists to generate the test_key.pem, test_cert.pem,
# ca_key.pem, and ca_cert.pem files.  It shouldn't really ever be run again
# until the year 2041 (when these certs expire)., unless you need to change
# or update attributes of the certificate (Common Name, Organization,
# whatever).

set -e

DAYS=10000

# Generate Certificate Authority Key
openssl genrsa -out ca_key.pem 2048

# Generate Self-Signed Certificate Authority Cert
openssl req -x509 -new -nodes \
    -key ca_key.pem \
    -days ${DAYS} \
    -out ca_cert.pem \
    -subj '/C=US/O=Asox/CN=Asox Certification Authority'

# CA serial number
echo 00000009 > ca_cert.srl

# Generate the test key
openssl genrsa -out test_key.pem 2048

function generateCert() {
    cat > conf.tmp <<EOF
prompt = no
[req]
distinguished_name = req_distinguished_name
req_extensions = v3_req

[req_distinguished_name]
countryName = US
stateOrProvinceName = Ohio
localityName = Hilliard
commonName = Asox Company

[v3_req]
# Extensions to add to a certificate request
# basicConstraints = CA:FALSE
# keyUsage = nonRepudiation, digitalSignature, keyEncipherment
subjectAltName = IP:$3, IP:$4
EOF

    # Generate the test key certificate request
    openssl req -new -nodes \
	-config conf.tmp \
	-key $1 \
	-days ${DAYS} \
	-out test_cert.csr \
	-subj '/C=US/O=Asox/CN=Asox Town Limited'

    # Sign the test key
    openssl x509 -req \
	-extensions v3_req \
	-extfile conf.tmp \
	-days ${DAYS} \
	-in test_cert.csr \
	-CA ca_cert.pem \
	-CAkey ca_key.pem \
	-out $2

    # Clean up the signing request as well as the serial number
    rm ca_cert.srl test_cert.csr conf.tmp
}

generateCert test_key.pem test_cert.pem 127.0.0.1 ::1
