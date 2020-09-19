#pragma title ("Copyright (c) 1991  Stephen Best")

#define     INCL_DOS
#define     INCL_WIN
#include    <os2.h>

#include    "bitware.h"
#include    "dialogs.h"

#define     MAX_SEARCH        10
#define     UM_SEARCHUPDATE   WM_SEM2

typedef struct
   {
   ULONG          ulDiskList;
   CHAR           szFileName [CCHMAXPATH];
   CHAR           szFileMask [CCHMAXPATHCOMP];
   } FILEPARMS;

typedef struct
   {
   TID            tid;
   BOOL           volatile fInterrupt;
   HEV            hevTrigger, hevTerminate;
   HWND           hwndOwner;
   ULONG          ulDiskList, volatile cNameIn, volatile cNameOut;
   CHAR           szSearchMask [CCHMAXPATH];
   CHAR           chFoundType [MAX_SEARCH];
   CHAR           szFoundName [MAX_SEARCH] [CCHMAXPATH];
   } THREADPARMS;

VOID EXPENTRY FileSearchThread (THREADPARMS *);
VOID          FileSearchFiles (THREADPARMS *, PSZ);
VOID          FileSearchUpdate (THREADPARMS *, CHAR, PSZ);
ULONG         FileQueryDisks (HWND);
VOID          FileListDisks (HWND, ULONG);

MRESULT EXPENTRY FileSearchProc
   (
   HWND           hwnd,       // window handle
   USHORT         msg,        // message
   MPARAM         mp1,        // message parameter 1
   MPARAM         mp2         // message parameter 2
   )

   {
   struct WINDATA
      {
      HWND           hwndStatus, hwndDisks, hwndFiles, hwndMask;
      HWND           hwndSearchButton, hwndOpenButton;
      BOOL           fSearchActive;
      CHAR           szSearchMask [CCHMAXPATH], szSelectFile [CCHMAXPATH];
      FILEPARMS      *pprmFile;
      THREADPARMS    prmThread;
      } register *pw;

         // Get pointer to window static data

   pw = WinQueryWindowPtr (hwnd, 0);

   switch ( msg )
      {

      case WM_INITDLG:

         {
               // Allocate area for window static and set pointer

         pw = malloc (sizeof (struct WINDATA));

         WinSetWindowPtr (hwnd, 0, pw);

               // Get window and pushbutton handles

         pw->hwndStatus = WinWindowFromID (hwnd, IDD_FILESEARCHSTATUS);
         pw->hwndDisks  = WinWindowFromID (hwnd, IDD_FILESEARCHDISKS);
         pw->hwndFiles  = WinWindowFromID (hwnd, IDD_FILESEARCHFILES);
         pw->hwndMask   = WinWindowFromID (hwnd, IDD_FILESEARCHMASK);

         pw->hwndSearchButton = WinWindowFromID (hwnd, IDD_SEARCH);
         pw->hwndOpenButton   = WinWindowFromID (hwnd, IDD_OK);

               // Set entry text limit

         WinSendMsg (pw->hwndMask, EM_SETTEXTLIMIT,
                     MPFROMSHORT (CCHMAXPATH), NULL);

               // Copy input parameters and save address

         pw->pprmFile = PVOIDFROMMP (mp2);

         FileListDisks (pw->hwndDisks, pw->pprmFile->ulDiskList);

         strcpy (pw->szSearchMask, pw->pprmFile->szFileMask);
         WinSetWindowText (pw->hwndMask, pw->szSearchMask);

               // Set search active initial status

         pw->fSearchActive = FALSE;

               // Create search thread

         DosCreateEventSem (NULL, &pw->prmThread.hevTrigger, NULL, FALSE);
         DosCreateEventSem (NULL, &pw->prmThread.hevTerminate, NULL, FALSE);

         DosCreateThread (&pw->prmThread.tid, (PFNTHREAD) FileSearchThread,
                     (ULONG) &pw->prmThread, FALSE, 64 * 1024);

         return 0;
         }

      case WM_COMMAND:

         {
         switch ( SHORT1FROMMP (mp1) )
            {

            case IDD_SEARCH:

               {
               ULONG          ulDiskList;

                     // Query search thread status

               if ( !pw->fSearchActive )
                  {
                        // Empty file list and disable open button

                  WinSendMsg (pw->hwndFiles, LM_DELETEALL, NULL, NULL);

                  WinEnableWindow (pw->hwndOpenButton, FALSE);

                        // Get search message, beep if none

                  if ( !WinQueryWindowText (pw->hwndMask,
                              sizeof pw->szSearchMask, pw->szSearchMask) )
                     {
                     WinAlarm (HWND_DESKTOP, WA_ERROR);

                     return 0;
                     }

                        // Get list of disks specified, beep if none

                  if ( (ulDiskList = FileQueryDisks (pw->hwndDisks)) == 0 )
                     {
                     WinAlarm (HWND_DESKTOP, WA_ERROR);

                     return 0;
                     }

                        // Set search parameters

                  pw->prmThread.cNameIn    = pw->prmThread.cNameOut = 0;
                  pw->prmThread.ulDiskList = ulDiskList;
                  pw->prmThread.hwndOwner  = hwnd;

                  strcpy (pw->prmThread.szSearchMask, pw->szSearchMask);

                        // Reset interrupt flag and trigger search

                  pw->prmThread.fInterrupt = FALSE;

                  DosPostEventSem (pw->prmThread.hevTrigger);

                        // Set search active status

                  pw->fSearchActive = TRUE;

                        // Update search pushbutton text

                  WinEnableWindow (pw->hwndDisks, FALSE);
                  WinEnableWindow (pw->hwndFiles, TRUE);
                  WinEnableWindow (pw->hwndMask, FALSE);
                  WinSetWindowText (pw->hwndSearchButton, "~Stop");
                  }
               else
                        // Set interrupt flag to stop current search

                  pw->prmThread.fInterrupt = TRUE;

               return 0;
               }

            case IDD_OK:

               {
                     // Return selected file and disks

               pw->pprmFile->ulDiskList = FileQueryDisks (pw->hwndDisks);
               strcpy (pw->pprmFile->szFileName, pw->szSelectFile);
               strcpy (pw->pprmFile->szFileMask, pw->szSearchMask);

                     // Terminate dialog

               WinDismissDlg (hwnd, TRUE);

               return 0;
               }

            case IDD_CANCEL:

               {
                     // Terminate dialog

               WinDismissDlg (hwnd, FALSE);

               return 0;
               }

            }

         break;
         }

      case WM_CONTROL:

         {
         USHORT         sItem;

         if ( SHORT1FROMMP (mp1) == IDD_FILESEARCHFILES )
            switch ( SHORT2FROMMP (mp1) )
               {

                     // File selected, get file and enable open button

               case LN_SELECT:

                  if ( (sItem = SHORT1FROMMR (WinSendMsg (pw->hwndFiles,
                              LM_QUERYSELECTION, NULL, NULL))) != LIT_NONE )
                     {
                     WinSendMsg (pw->hwndFiles, LM_QUERYITEMTEXT,
                                 MPFROM2SHORT (sItem, sizeof pw->szSelectFile),
                                 pw->szSelectFile);

                     WinEnableWindow (pw->hwndOpenButton, TRUE);
                     }
                  else
                     WinEnableWindow (pw->hwndOpenButton, FALSE);

                  return 0;

                     // Double click on file name, exit with file name

               case LN_ENTER:

                  WinPostMsg (hwnd, WM_COMMAND, MPFROMSHORT (IDD_OK), NULL);

                  return 0;

               }
         break;
         }

      case UM_SEARCHUPDATE:

         {
         ULONG          register iEntry;

               // Add file name(s) to listbox until all done

         for ( ; pw->prmThread.cNameOut < pw->prmThread.cNameIn;
                     pw->prmThread.cNameOut++ )
            {
            iEntry = pw->prmThread.cNameOut % MAX_SEARCH;

            if ( pw->prmThread.chFoundType [iEntry] == 'D' )
               WinSetWindowText (pw->hwndStatus,
                           pw->prmThread.szFoundName [iEntry]);
            else
               WinSendMsg (pw->hwndFiles, LM_INSERTITEM, MPFROMSHORT (LIT_END),
                           pw->prmThread.szFoundName [iEntry]);
            }

         if ( mp1 )
            {
                  // Reset search active status

            pw->fSearchActive = FALSE;

                  // Reset display

            WinSetWindowText (pw->hwndStatus, "");
            WinSetWindowText (pw->hwndSearchButton, "~Search");
            WinEnableWindow (pw->hwndDisks, TRUE);
            WinEnableWindow (pw->hwndMask, TRUE);

                  // If no files, set text and disable listbox

            if ( !WinSendMsg (pw->hwndFiles, LM_QUERYITEMCOUNT, NULL, NULL) )
               {
               WinEnableWindow (pw->hwndFiles, FALSE);
               WinSendMsg (pw->hwndFiles, LM_INSERTITEM, 0, "No Files!");
               }
            }

         return 0;
         }

      case WM_DESTROY:

         {
               // Signal termination of search thread and wait for completion

         pw->prmThread.fInterrupt = TRUE;

         DosPostEventSem (pw->prmThread.hevTerminate);

         DosWaitThread (&pw->prmThread.tid, DCWW_WAIT);

               // Free event semaphores

         DosCloseEventSem (pw->prmThread.hevTrigger);
         DosCloseEventSem (pw->prmThread.hevTerminate);

               // Release area for window data

         free (pw);

         return 0;
         }

      }
   return ( WinDefDlgProc (hwnd, msg, mp1, mp2) );
   }

VOID EXPENTRY FileSearchThread
   (
   THREADPARMS    register *pt
   )

   {
   HAB            hab;
   HMUX           hmux;
   SEMRECORD      apsr [2];
   ULONG          ulSem, cPosts, ulDiskList;
   CHAR           szSearchDir [CCHMAXPATH];

         // Initialize thread

   hab = WinInitialize (0);

         // Create muxwait for trigger and terminate event semaphores

   apsr [0].hsemCur = (HSEM) pt->hevTrigger;
   apsr [0].ulUser  = FALSE;
   apsr [1].hsemCur = (HSEM) pt->hevTerminate;
   apsr [1].ulUser  = TRUE;

   DosCreateMuxWaitSem (NULL, &hmux, 2, apsr, DCMW_WAIT_ANY);

         // Search pass loop

   while ( TRUE )
      {
            // Wait for either trigger or terminate, exit on terminate

      DosWaitMuxWaitSem (hmux, SEM_INDEFINITE_WAIT, &ulSem);

      if ( ulSem )
         break;

            // Start search for each disk specified

      strcpy (szSearchDir, "A:\\");

      for ( ulDiskList = pt->ulDiskList; !pt->fInterrupt && ulDiskList != 0;
                  ulDiskList >>= 1 )
         {
         if ( ulDiskList & 1 )
            {
            FileSearchUpdate (pt, 'D', szSearchDir);

            FileSearchFiles (pt, szSearchDir);
            }

         ++szSearchDir [0];
         }

            // Reset trigger and indicate now idle

      DosResetEventSem (pt->hevTrigger, &cPosts);

            // Search complete (or interrupted), post completion message

      WinPostMsg (pt->hwndOwner, UM_SEARCHUPDATE, MPFROMSHORT (TRUE), NULL);
      }

         // Delete muxwait semaphore

   DosCloseMuxWaitSem (hmux);

         // Terminate thread

   WinTerminate (hab);
   }

VOID FileSearchFiles
   (
   THREADPARMS    register *pt,  		// thread parameters
   PSZ            pszSearchDir         // current search path
   )

   {
   HDIR           hdir;
   ULONG          ulSearchCount;
   FILEFINDBUF3   findbuf;
   CHAR           szSearchName [CCHMAXPATH], szFoundName [CCHMAXPATH];

         // Search for matching files

   if ( !pt->fInterrupt )
      {
      strcpy (szSearchName, pszSearchDir);
      strcat (szSearchName, pt->szSearchMask);

      hdir          = HDIR_CREATE;
      ulSearchCount = 1;

      if ( !DosFindFirst (szSearchName, &hdir, FILE_NORMAL, &findbuf,
						sizeof findbuf, &ulSearchCount, FIL_STANDARD) )
         {
         do
            {
            strcpy (szFoundName, pszSearchDir);
            strcat (szFoundName, findbuf.achName);

            FileSearchUpdate (pt, 'F', szFoundName);

            ulSearchCount = 1;
            }
         while ( !pt->fInterrupt && !DosFindNext (hdir, &findbuf,
                     sizeof findbuf, &ulSearchCount) );

         DosFindClose (hdir);
         }
      }

         // Search for sub-directories

   if ( !pt->fInterrupt )
      {
      strcpy (szSearchName, pszSearchDir);
      strcat (szSearchName, "*");

      hdir          = HDIR_CREATE;
      ulSearchCount = 1;

      if ( !DosFindFirst (szSearchName, &hdir, FILE_DIRECTORY, &findbuf,
                  sizeof findbuf, &ulSearchCount, FIL_STANDARD) )
         {
         do
            {
            if ( (findbuf.attrFile & FILE_DIRECTORY) &&
                        (findbuf.achName [0] != '.') )
               {
               strcpy (szFoundName, pszSearchDir);
               strcat (szFoundName, findbuf.achName);

               FileSearchUpdate (pt, 'D', szFoundName);

               strcat (szFoundName, "\\");

               FileSearchFiles (pt, szFoundName);
               }

            ulSearchCount = 1;
            }
         while ( !pt->fInterrupt && !DosFindNext (hdir, &findbuf,
                     sizeof findbuf, &ulSearchCount) );

         DosFindClose (hdir);
         }
      }
   }

VOID FileSearchUpdate
   (
   THREADPARMS    register *pt,
   CHAR           chUpdateType,
   PSZ            pszFoundName
   )

   {
   ULONG          register iEntry;

         // Check if buffer full, if so delay until slot(s) free

   while ( !pt->fInterrupt && (pt->cNameIn - pt->cNameOut) == MAX_SEARCH )
      DosSleep (0);

         // Move entry to free slot and signal update required

   if ( !pt->fInterrupt )
      {
      iEntry = pt->cNameIn % MAX_SEARCH;

      pt->chFoundType [iEntry] = chUpdateType;

      strcpy (pt->szFoundName [iEntry], pszFoundName);

      ++pt->cNameIn;

      WinPostMsg (pt->hwndOwner, UM_SEARCHUPDATE, FALSE, NULL);
      }
   }

ULONG FileQueryDisks
   (
   HWND           hwndList
   )

   {
   ULONG          ulDiskList;
   SHORT          sItem;
   CHAR           szItemText [3];

         // Get selected disks from multiple selection listbox

   ulDiskList = 0;

   sItem = LIT_FIRST;
   while ( (sItem = SHORT1FROMMR (WinSendMsg (hwndList, LM_QUERYSELECTION,
               MPFROMSHORT (sItem), NULL)))  != LIT_NONE )
      {
      WinSendMsg (hwndList, LM_QUERYITEMTEXT,
                  MPFROM2SHORT (sItem, sizeof szItemText),
                  szItemText);

      ulDiskList |= 1 << (szItemText [0] - 'A');
      }

   return ( ulDiskList );
   }

VOID FileListDisks
   (
   HWND           hwndList,
   ULONG          ulDiskSelect
   )

   {
   ULONG          ulDriveNumber, ulLogicalDrives;
   CHAR           szListEntry [3];
   SHORT          sItem;

         // Set list of disks and highlight selected entries

   WinSendMsg (hwndList, LM_DELETEALL, NULL, NULL);

   WinEnableWindowUpdate (hwndList, FALSE);

   DosQueryCurrentDisk (&ulDriveNumber, &ulLogicalDrives);

   for ( strcpy (szListEntry, "A:"); ulLogicalDrives != 0;
               ++szListEntry [0], ulLogicalDrives >>= 1, ulDiskSelect >>= 1 )
      if ( ulLogicalDrives & 1 )
         {
         sItem = SHORT1FROMMR (WinSendMsg (hwndList, LM_INSERTITEM,
                     MPFROMSHORT (LIT_END), szListEntry));

         if ( ulDiskSelect & 1 )
            WinSendMsg (hwndList, LM_SELECTITEM, MPFROMSHORT (sItem),
                        MPFROMSHORT (TRUE));
         }

   WinShowWindow (hwndList, TRUE);
   }

