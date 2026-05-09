#!/opt/tigersh-deps-0.1/bin/bash
# Session 055 sanity baseline — match the handoff's "How to pick up" script.
set -e -o pipefail
cd ~/tcc-darwin8-ppc
echo "=== HEAD ==="
git log --oneline -1
echo
echo "=== build ==="
PATH=/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh configure
echo
echo "=== bootstrap fixpoint ==="
FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh
echo
echo "=== tests2 ==="
./scripts/run-tests2.sh 2>&1 | tail -8
echo
echo "=== demos ==="
for d in v0.2.32-longdouble v0.2.33-libtcc-callback v0.2.40-sed v0.2.41-gzip v0.2.42-python v0.2.43-gdebug-extern v0.2.44-gdebug-link; do
    if ./demos/${d}.sh > /Users/macuser/tmp/demo-${d}.log 2>&1; then
        echo "PASS  ${d}"
    else
        echo "FAIL  ${d} (see /Users/macuser/tmp/demo-${d}.log)"
    fi
done
echo
echo "=== upstream tests ==="
cd tcc/tests
PATH=/opt/make-4.3/bin:$PATH /opt/make-4.3/bin/make abitest-cc abitest-tcc libtest dlltest 2>&1 | tail -30
echo
echo "=== DONE ==="
