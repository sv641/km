#!/usr/bin/env bash

set -x
set -e

pod=$(kubectl get pods --template '{{range .items}}{{.metadata.name}}{{"\n"}}{{end}}' -A -l kontain=test-app)

kubectl exec -it ${pod} -- uname -r
