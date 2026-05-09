#!/opt/tigersh-deps-0.1/bin/bash
# Post-campaign analysis: triage FAILs in csmith SUMMARY.txt into real
# bugs vs. both-timeout false alarms. Run on imacg3.
set -u

OUT=/Users/macuser/tmp/csmith-out
SUMMARY="$OUT/SUMMARY.txt"

both_to=0; tcc_only=0; real=0

while IFS= read -r line; do
    case "$line" in
    "FAIL "*"(output-diff)")
        seed=$(echo "$line" | sed -E 's/FAIL ([0-9]+).*/\1/')
        g_to=0; t_to=0
        if grep -q "Alarm clock" "$OUT/seed-$seed.gcc.out" 2>/dev/null; then
            g_to=1
        fi
        if grep -q "Alarm clock" "$OUT/seed-$seed.tcc.out" 2>/dev/null; then
            t_to=1
        fi
        if [ $g_to -eq 1 ] && [ $t_to -eq 1 ]; then
            both_to=$((both_to+1))
            echo "BOTH-TIMEOUT seed=$seed"
        elif [ $t_to -eq 1 ] && [ $g_to -eq 0 ]; then
            tcc_only=$((tcc_only+1))
            echo "TCC-ONLY-TIMEOUT seed=$seed"
        else
            real=$((real+1))
            echo "REAL-BUG seed=$seed"
        fi
        ;;
    esac
done < "$SUMMARY"

echo "==="
echo "both-timeout (false alarm): $both_to"
echo "tcc-only-timeout (real bug, infinite loop): $tcc_only"
echo "real bug (output diff):     $real"
