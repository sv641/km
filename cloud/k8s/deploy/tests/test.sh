#!/usr/bin/env bash

set -x
set -e

kubectl apply -f demo/k8s/test.yaml 

sleep 5

# Wait for test pod to be ready.
kubectl wait --for=condition=ready pod -l kontain=test-app

# Get name of test pod
pod=$(kubectl get pods --template '{{range .items}}{{.metadata.name}}{{"\n"}}{{end}}' -A -l kontain=test-app)

kubectl exec -it ${pod} -- uname -r

kubectl delete -f demo/k8s/test.yaml 
