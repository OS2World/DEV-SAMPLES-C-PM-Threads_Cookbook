#pragma title ("Copyright (c) 1991  Stephen Best")

#define     INCL_DOS
#define     INCL_WIN
#define     INCL_GPI
#include    <os2.h>

#include    "bitware.h"

#define     UM_SHADOWCREATE   WM_USER
#define     UM_SHADOWDELETE   WM_USER + 1
#define     UM_WINDOWUPDATE   WM_USER + 2
#define     UM_WINDOWNUMBER   WM_USER + 3
#define     UM_WINDOWCLEAR    WM_USER + 4

typedef struct
   {
   TID            tid;
   HMQ            hmq;
   HEV            hevReady;
   HWND           hwndOwner;
   } THREADPARMS;

VOID EXPENTRY VectorThread (THREADPARMS *);

MRESULT EXPENTRY VectorProc
   (
   HWND           hwnd,       // window handle
   USHORT         msg,        // message
   MPARAM         mp1,        // message parameter 1
   MPARAM         mp2         // message parameter 2
   )

   {
   struct WINDATA
      {
      HWND           hwndFrame;
      LONG           idWindow;
      HPS            hpsShadow;
      SHORT          cxWindow, cyWindow, cPostsPending;
      THREADPARMS    prmThread;
      } register *pw;

         // Get pointer to window static data

   pw = WinQueryWindowPtr (hwnd, 0);

   switch ( msg )
      {

      case WM_CREATE:

         {
               // Allocate area for window static and set pointer

         pw = malloc (sizeof (struct WINDATA));

         WinSetWindowPtr (hwnd, 0, pw);

               // Initialize static variables

         pw->hwndFrame     = WinQueryWindow (hwnd, QW_PARENT);
         pw->hpsShadow     = NULL;
         pw->cxWindow      = pw->cyWindow = 0;
         pw->cPostsPending = 0;
         pw->idWindow      = 0;

         pw->prmThread.hwndOwner = hwnd;

               // Create search thread

         DosCreateEventSem (NULL, &pw->prmThread.hevReady, NULL, FALSE);

         DosCreateThread (&pw->prmThread.tid, (PFNTHREAD) VectorThread,
                     (ULONG) &pw->prmThread, FALSE, 16 * 1024);

         DosWaitEventSem (pw->prmThread.hevReady, SEM_INDEFINITE_WAIT);

         DosCloseEventSem (pw->prmThread.hevReady);

               // Drop priority of search thread

         DosSetPriority (PRTYS_THREAD, PRTYC_NOCHANGE, -2, pw->prmThread.tid);

         return 0;
         }

      case UM_WINDOWNUMBER:

         {
               // Input window number, clear window and signal shadow creation

         pw->idWindow = LONGFROMMP (mp1);

         if ( (pw->cxWindow != 0) && (pw->cyWindow != 0) )
            {
            pw->hpsShadow = NULL;

            WinInvalidateRect (hwnd, NULL, FALSE);

            while ( !WinPostQueueMsg (pw->prmThread.hmq, UM_SHADOWCREATE,
                        MPFROM2SHORT (pw->cxWindow, pw->cyWindow), NULL) )
               DosSleep (100);

            ++pw->cPostsPending;
            }

         return 0;
         }

      case UM_WINDOWCLEAR:

         {
               // Clear window request, clear window and signal shadow deletion

         pw->idWindow = 0;

         pw->hpsShadow = NULL;

         WinInvalidateRect (hwnd, NULL, FALSE);

         while ( !WinPostQueueMsg (pw->prmThread.hmq, UM_SHADOWDELETE, NULL,
                     NULL) )
            DosSleep (100);

         ++pw->cPostsPending;

         return 0;
         }

      case WM_ACTIVATE:

         {
               // Window activation/deactivation, change relative priority

         DosSetPriority (PRTYS_THREAD, PRTYC_NOCHANGE, ( mp1 ? +1 : -1 ),
                     pw->prmThread.tid);

         return 0;
         }

      case WM_SIZE:

         {
               // Change in window size, signal new shadow creation

         pw->cxWindow = SHORT1FROMMP (mp2);
         pw->cyWindow = SHORT2FROMMP (mp2);

         if ( pw->idWindow != 0 )
            {
            pw->hpsShadow = NULL;

            WinInvalidateRect (hwnd, NULL, FALSE);

            while ( !WinPostQueueMsg (pw->prmThread.hmq, UM_SHADOWCREATE,
                        MPFROMLONG (pw->idWindow), mp2) )
               DosSleep (100);

            ++pw->cPostsPending;
            }

         return 0;
         }

      case UM_WINDOWUPDATE:

         {
               // Request complete, check if response from latest request

         if ( --pw->cPostsPending == 0 )
            {
            pw->hpsShadow = mp1;

            WinInvalidateRect (hwnd, NULL, FALSE);
            }

         return 0;
         }

      case WM_PAINT:

         {
         HPS            hps;
         RECTL          rcl;
         POINTL         aptl [3];

               // If shadow exists copy to window, else clear window

         hps = WinBeginPaint (hwnd, NULL, &rcl);

         if ( pw->hpsShadow != NULL )
            {
            aptl [0].x = aptl [2].x = rcl.xLeft;
            aptl [0].y = aptl [2].y = rcl.yBottom;
            aptl [1].x = rcl.xRight;
            aptl [1].y = rcl.yTop;

            GpiBitBlt (hps, pw->hpsShadow, 3, aptl, ROP_NOTSRCCOPY, NULL);
            }
         else
            WinFillRect (hps, &rcl, CLR_WHITE);

         WinEndPaint (hps);

         return 0;
         }

      case WM_CLOSE:

         {
               // Close request, destroy window and prevent WM_QUIT

         WinDestroyWindow (pw->hwndFrame);

         return 0;
         }

      case WM_DESTROY:

         {
               // Delete shadow window

         while ( !WinPostQueueMsg (pw->prmThread.hmq, UM_SHADOWDELETE, NULL,
                     NULL) )
            DosSleep (100);

               // Signal termination of search thread and wait for completion

         while ( !WinPostQueueMsg (pw->prmThread.hmq, WM_QUIT, NULL, NULL) )
            DosSleep (100);

         DosWaitThread (&pw->prmThread.tid, DCWW_WAIT);

               // Release area for window data

         free (pw);

         return 0;
         }

      }
   return ( WinDefWindowProc (hwnd, msg, mp1, mp2) );
   }

VOID EXPENTRY VectorThread
   (
   THREADPARMS    /* register */ *pt
   )

   {
   HAB            hab;
   QMSG           qmsg;
   HDC            hdc;
   HPS            hps;
   SIZEL          sizl;
   FATTRS         fat;

         // Initialize thread

   hab = WinInitialize (0);

         // Create message queue and indicate ready

   pt->hmq = WinCreateMsgQueue (hab, 0);

   DosPostEventSem (pt->hevReady);

         // Create memory DC and associated PS for shadow bitmap

   hdc = DevOpenDC (hab, OD_MEMORY, "*", 0, NULL, NULL);

   sizl.cx = sizl.cy = 0;

   hps = GpiCreatePS (hab, hdc, &sizl,
               PU_PELS | GPIF_DEFAULT | GPIT_MICRO | GPIA_ASSOC);

         // Set "Courier" vector font for subsequent drawing

   fat.usRecordLength  = sizeof (FATTRS);
   fat.fsSelection     = 0;
   fat.lMatch          = 0;
   fat.idRegistry      = 0;
   fat.usCodePage      = 0;
   fat.lMaxBaselineExt = 0;
   fat.lAveCharWidth   = 0;
   fat.fsType          = 0;
   fat.fsFontUse       = FATTR_FONTUSE_OUTLINE |
                         FATTR_FONTUSE_TRANSFORMABLE;

   strcpy (fat.szFacename, "Courier");

   GpiCreateLogFont (hps, NULL, 1, &fat);
   GpiSetCharSet (hps, 1);

         // Message loop processing

   while ( WinGetMsg (hab, &qmsg, NULL, 0, 0) )
      switch ( qmsg.msg )
         {

         case UM_SHADOWCREATE:

            {
            SIZEF             sizfx;
            POINTL            ptl, aptl [TXTBOX_COUNT];
            HBITMAP           hbm;
            BITMAPINFOHEADER  bmp;
            CHAR              szText [50];
            SHORT             cbText;

                  // Check for second request queued, if so skip current

            if ( WinQueryQueueStatus (HWND_DESKTOP) )
               {
               while ( !WinPostMsg (pt->hwndOwner, UM_WINDOWUPDATE, NULL,
                           NULL) )
                  DosSleep (0);

               break;
               }

                  // Convert input window number

            cbText = (SHORT) strlen (itoa ( (LONG) qmsg.mp1, szText, 10));

                  // Create bitmap to match current window size

            if ( (hbm = GpiSetBitmap (hps, NULL)) != NULL )
               GpiDeleteBitmap (hbm);

            bmp.cbFix     = sizeof bmp;
            bmp.cx        = SHORT1FROMMP (qmsg.mp2);
            bmp.cy        = SHORT2FROMMP (qmsg.mp2);
            bmp.cPlanes   = 1;
            bmp.cBitCount = 1;

            hbm = GpiCreateBitmap (hps, (PBITMAPINFOHEADER2) &bmp, NULL, NULL,
                        NULL);

            GpiSetBitmap (hps, hbm);

                  // Clear bitmap

            GpiErase (hps);

                  // Compute character size and start offsets

            sizfx.cx = sizfx.cy = min (bmp.cx / cbText, bmp.cy) << 16;

            GpiSetCharBox (hps, &sizfx);

            GpiQueryTextBox (hps, cbText, szText, TXTBOX_COUNT, aptl);

            ptl.x = (bmp.cx - aptl [TXTBOX_BOTTOMRIGHT].x) / 2;
            ptl.y = bmp.cy / 2 + aptl [TXTBOX_BOTTOMRIGHT].y;

                  // Draw text and stroke outline

            GpiBeginPath (hps, 1);
            GpiCharStringAt (hps, &ptl, cbText, szText);
            GpiEndPath (hps);

            GpiStrokePath (hps, 1, NULL);

                  // Signal done and return handle to shadow PS

            while ( !WinPostMsg (pt->hwndOwner, UM_WINDOWUPDATE, hps, NULL) )
               DosSleep (0);

            break;
            }

         case UM_SHADOWDELETE:

            {
            HBITMAP        hbm;

                  // Delete current shadow bitmap

            if ( (hbm = GpiSetBitmap (hps, NULL)) != NULL )
               GpiDeleteBitmap (hbm);

                  // Signal done

            while ( !WinPostMsg (pt->hwndOwner, UM_WINDOWUPDATE, NULL, NULL) )
               DosSleep (0);

            break;
            }

         }

         // Restore default font

   GpiSetCharSet (hps, LCID_DEFAULT);
   GpiDeleteSetId (hps, 1);

         // Destroy shadow DC and PS

   GpiDestroyPS (hps);

   DevCloseDC (hdc);

         // Destroy thread message queue and terminate thread

   WinDestroyMsgQueue (pt->hmq);

   WinTerminate (hab);
   }
