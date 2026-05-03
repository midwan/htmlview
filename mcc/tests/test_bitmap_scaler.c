/* Host-side unit test for BitmapScaler::ComputeScaledExtent.

   The scaling helpers themselves call into graphics.library and are
   verified visually via SimpleTest on real hardware/emulator. This
   test pins the dimension-rounding rules so a future "smarter"
   ComputeScaledExtent implementation can't silently drift.

   Build:
     gcc -DBMSCALER_HOST_TEST -I.. -o tbs \
         test_bitmap_scaler.c ../BitmapScaler.cpp
     ./tbs
*/

#include <assert.h>
#include <stdio.h>

/* BitmapScaler.h, with BMSCALER_HOST_TEST defined, supplies the AmigaOS
   typedefs (UWORD/ULONG/etc.) itself so we don't need <exec/types.h>. */
#include "../BitmapScaler.h"

int main(void)
{
    UWORD w, h;

    ComputeScaledExtent(64, 64, 64, 64, &w, &h);
    assert(w == 64 && h == 64);

    ComputeScaledExtent(400, 400, 64, 64, &w, &h);
    assert(w == 64 && h == 64);

    ComputeScaledExtent(16, 16, 64, 64, &w, &h);
    assert(w == 64 && h == 64);

    ComputeScaledExtent(200, 100, 50, 100, &w, &h);
    assert(w == 50 && h == 100);

    /* dst = 0 means "keep src extent" — used by callers that scale
       only one axis. */
    ComputeScaledExtent(123, 45, 0, 45, &w, &h);
    assert(w == 123 && h == 45);

    ComputeScaledExtent(123, 45, 99, 0, &w, &h);
    assert(w == 99 && h == 45);

    printf("test_bitmap_scaler OK\n");
    return 0;
}
