* CMSAWK 0.1: ONE-RULE AWK SUBSET FOR CMS, FIXED 80-BYTE RECORDS.       CMS00010
* USE CMSAWK SCRIPT-ID DATA-ID; EACH ID IS FILENAME FILETYPE MODE.      CMS00020
* ONE LOWERCASE PRINT RULE; $0 OR ONE FIELD $1-$9; BLANKS SPLIT FIELDS. CMS00030
* OPTIONAL PATTERN IS A CASE-SENSITIVE LITERAL SUBSTRING, NOT A REGEX.  CMS00040
CMSAWK  CSECT                                                           CMS00050
         STM   R14,R12,12(R13)                                          CMS00060
         LR    R12,R15                                                  CMS00070
         USING CMSAWK,R12                                               CMS00080
         LA    R11,SAVE01                                               CMS00090
         ST    R11,8(,R13)                                              CMS00100
         ST    R13,4(,R11)                                              CMS00110
         LR    R13,R11                                                  CMS00120
         BALR  R12,0                                                    CMS00130
         USING *,R12                                                    CMS00140
         USING PLIST,R1                                                 CMS00150
         MVC   SCRFID(24),SCRIPT                                        CMS00160
         MVC   DATFID(24),DATAFILE                                      CMS00170
         MVC   SOPEN,=F'0'                                              CMS00180
         MVC   DOPEN,=F'0'                                              CMS00190
         MVC   SFSCB,SFSCBI                                             CMS00200
         LA    R2,SCRFID                                                CMS00210
         LA    R4,SCRBUF                                                CMS00220
         FSOPEN (R2),FSCB=SFSCB,BUFFER=(R4),FORM=E                      CMS00230
         LTR   R15,R15                                                  CMS00240
         BNZ   OPENERR                                                  CMS00250
         MVC   SOPEN,=F'1'                                              CMS00260
         MVC   SCRBUF(80),BLANK80                                       CMS00270
         FSREAD FSCB=SFSCB,ERROR=SREADER,FORM=E                         CMS00280
         LTR   R15,R15                                                  CMS00290
         BNZ   SREADER                                                  CMS00300
         LA    R6,80                                                    CMS00310
TRIMS    LA    R5,SCRBUF-1(R6)                                          CMS00320
         CLI   0(R5),C' '                                               CMS00330
         BNE   SREADY                                                   CMS00340
         BCTR  R6,0                                                     CMS00350
         LTR   R6,R6                                                    CMS00360
         BNZ   TRIMS                                                    CMS00370
SREADY   ST    R6,SLEN                                                  CMS00380
         SR    R8,R8                                                    CMS00390
         ST    R8,PATLEN                                                CMS00400
         ST    R8,FIELDNO                                               CMS00410
         LA    R7,1                                                     CMS00420
         LA    R5,SCRBUF                                                CMS00430
         CLI   0(R5),C'/'                                               CMS00440
         BNE   SKIPSP                                                   CMS00450
         LA    R7,2                                                     CMS00460
PATLOOP  C     R7,SLEN                                                  CMS00470
         BH    BADRULE                                                  CMS00480
         LA    R5,SCRBUF-1(R7)                                          CMS00490
         CLI   0(R5),C'/'                                               CMS00500
         BE    PATEND                                                   CMS00510
         IC    R0,0(R5)                                                 CMS00520
         LA    R8,1(R8)                                                 CMS00530
         C     R8,=F'80'                                                CMS00540
         BH    BADRULE                                                  CMS00550
         LA    R4,PATTERN-1(R8)                                         CMS00560
         STC   R0,0(R4)                                                 CMS00570
         LA    R7,1(R7)                                                 CMS00580
         B     PATLOOP                                                  CMS00590
PATEND   ST    R8,PATLEN                                                CMS00600
         LA    R7,1(R7)                                                 CMS00610
SKIPSP   C     R7,SLEN                                                  CMS00620
         BH    BADRULE                                                  CMS00630
         LA    R5,SCRBUF-1(R7)                                          CMS00640
         CLI   0(R5),C' '                                               CMS00650
         BNE   OPENBR                                                   CMS00660
         LA    R7,1(R7)                                                 CMS00670
         B     SKIPSP                                                   CMS00680
OPENBR   CLI   0(R5),C'{'                                               CMS00690
         BNE   BADRULE                                                  CMS00700
         LA    R7,1(R7)                                                 CMS00710
SPPRINT  C     R7,SLEN                                                  CMS00720
         BH    BADRULE                                                  CMS00730
         LA    R5,SCRBUF-1(R7)                                          CMS00740
         CLI   0(R5),C' '                                               CMS00750
         BNE   ISPRINT                                                  CMS00760
         LA    R7,1(R7)                                                 CMS00770
         B     SPPRINT                                                  CMS00780
ISPRINT  CLC   0(5,R5),=X'97998995A3'                                   CMS00790
         BNE   BADRULE                                                  CMS00800
         LA    R7,5(R7)                                                 CMS00810
SPFIELD  C     R7,SLEN                                                  CMS00820
         BH    BADRULE                                                  CMS00830
         LA    R5,SCRBUF-1(R7)                                          CMS00840
         CLI   0(R5),C' '                                               CMS00850
         BNE   DOLLAR                                                   CMS00860
         LA    R7,1(R7)                                                 CMS00870
         B     SPFIELD                                                  CMS00880
DOLLAR   CLI   0(R5),C'$'                                               CMS00890
         BNE   BADRULE                                                  CMS00900
         LA    R7,1(R7)                                                 CMS00910
         C     R7,SLEN                                                  CMS00920
         BH    BADRULE                                                  CMS00930
         LA    R5,SCRBUF-1(R7)                                          CMS00940
         CLI   0(R5),C'0'                                               CMS00950
         BL    BADRULE                                                  CMS00960
         CLI   0(R5),C'9'                                               CMS00970
         BH    BADRULE                                                  CMS00980
         XR    R8,R8                                                    CMS00990
         IC    R8,0(R5)                                                 CMS01000
         S     R8,=F'240'                                               CMS01010
         ST    R8,FIELDNO                                               CMS01020
         LA    R7,1(R7)                                                 CMS01030
SPCLOSE  C     R7,SLEN                                                  CMS01040
         BH    BADRULE                                                  CMS01050
         LA    R5,SCRBUF-1(R7)                                          CMS01060
         CLI   0(R5),C' '                                               CMS01070
         BNE   CLOSEBR                                                  CMS01080
         LA    R7,1(R7)                                                 CMS01090
         B     SPCLOSE                                                  CMS01100
CLOSEBR  CLI   0(R5),C'}'                                               CMS01110
         BNE   BADRULE                                                  CMS01120
         B     OPENDAT                                                  CMS01130
BADRULE  APPLMSG TEXT='CMSAWK: USE { PRINT $0 } OR /TEXT/ { PRINT $1 }' CMS01140
         B     FAIL24                                                   CMS01150
BADFILE  APPLMSG TEXT='CMSAWK: INPUT MUST BE FIXED 80-BYTE RECORDS'     CMS01160
         B     FAIL24                                                   CMS01170
SREADER  APPLMSG TEXT='CMSAWK: CANNOT READ ONE-RULE SCRIPT FILE'        CMS01180
         B     FAIL8                                                    CMS01190
OPENERR  APPLMSG TEXT='CMSAWK: CANNOT OPEN SCRIPT FILE'                 CMS01200
         B     FAIL8                                                    CMS01210
OPENDAT  FSCLOSE FSCB=SFSCB                                             CMS01220
         MVC   SOPEN,=F'0'                                              CMS01230
         MVC   DFSCB,DFSCBI                                             CMS01240
         LA    R2,DATFID                                                CMS01250
         LA    R4,DATABUF                                               CMS01260
         FSOPEN (R2),FSCB=DFSCB,BUFFER=(R4),FORM=E                      CMS01270
         LTR   R15,R15                                                  CMS01280
         BNZ   DOPENER                                                  CMS01290
         MVC   DOPEN,=F'1'                                              CMS01300
READLOOP FSREAD FSCB=DFSCB,ERROR=DREADER,FORM=E                         CMS01310
         LTR   R15,R15                                                  CMS01320
         BNZ   DREADER                                                  CMS01330
         LA    R6,80                                                    CMS01340
TRIMD    LA    R5,DATABUF-1(R6)                                         CMS01350
         CLI   0(R5),C' '                                               CMS01360
         BNE   GOTREC                                                   CMS01370
         BCTR  R6,0                                                     CMS01380
         LTR   R6,R6                                                    CMS01390
         BNZ   TRIMD                                                    CMS01400
GOTREC   ST    R6,RECLEN                                                CMS01410
         L     R8,PATLEN                                                CMS01420
         LTR   R8,R8                                                    CMS01430
         BZ    FIELDS                                                   CMS01440
         L     R6,RECLEN                                                CMS01450
         CR    R6,R8                                                    CMS01460
         BL    READLOOP                                                 CMS01470
         SR    R6,R8                                                    CMS01480
         LA    R6,1(R6)                                                 CMS01490
         LA    R7,1                                                     CMS01500
PATTRY   CR    R7,R6                                                    CMS01510
         BH    READLOOP                                                 CMS01520
         LR    R3,R8                                                    CMS01530
         LA    R5,DATABUF-1(R7)                                         CMS01540
         LA    R4,PATTERN                                               CMS01550
PATBYTE  CLC   0(1,R5),0(R4)                                            CMS01560
         BNE   PATNEXT                                                  CMS01570
         LA    R5,1(R5)                                                 CMS01580
         LA    R4,1(R4)                                                 CMS01590
         BCT   R3,PATBYTE                                               CMS01600
         B     FIELDS                                                   CMS01610
PATNEXT  LA    R7,1(R7)                                                 CMS01620
         B     PATTRY                                                   CMS01630
FIELDS   XC    FSTART(10),FSTART                                        CMS01640
         XC    FLEN(10),FLEN                                            CMS01650
         SR    R8,R8                                                    CMS01660
         LA    R7,1                                                     CMS01670
FLNEXT   L     R6,RECLEN                                                CMS01680
         C     R7,RECLEN                                                CMS01690
         BH    FLDONE                                                   CMS01700
         LA    R5,DATABUF-1(R7)                                         CMS01710
         CLI   0(R5),C' '                                               CMS01720
         BNE   FLSTART                                                  CMS01730
         LA    R7,1(R7)                                                 CMS01740
         B     FLNEXT                                                   CMS01750
FLSTART  LA    R8,1(R8)                                                 CMS01760
         C     R8,=F'9'                                                 CMS01770
         BH    FLDONE                                                   CMS01780
         LA    R4,FSTART-1(R8)                                          CMS01790
         STC   R7,0(R4)                                                 CMS01800
         LR    R6,R7                                                    CMS01810
FLEND    C     R7,RECLEN                                                CMS01820
         BH    FLSTORE                                                  CMS01830
         LA    R5,DATABUF-1(R7)                                         CMS01840
         CLI   0(R5),C' '                                               CMS01850
         BE    FLSTORE                                                  CMS01860
         LA    R7,1(R7)                                                 CMS01870
         B     FLEND                                                    CMS01880
FLSTORE  LR    R0,R7                                                    CMS01890
         SR    R0,R6                                                    CMS01900
         LA    R4,FLEN-1(R8)                                            CMS01910
         STC   R0,0(R4)                                                 CMS01920
         B     FLNEXT                                                   CMS01930
FLDONE   L     R8,FIELDNO                                               CMS01940
         LTR   R8,R8                                                    CMS01950
         BZ    PRINT0                                                   CMS01960
         LA    R4,FSTART-1(R8)                                          CMS01970
         XR    R5,R5                                                    CMS01980
         IC    R5,0(R4)                                                 CMS01990
         LA    R5,DATABUF-1(R5)                                         CMS02000
         LA    R4,FLEN-1(R8)                                            CMS02010
         XR    R6,R6                                                    CMS02020
         IC    R6,0(R4)                                                 CMS02030
         LTR   R6,R6                                                    CMS02040
         BNZ   WRITEIT                                                  CMS02050
         LA    R5,OUTBUF                                                CMS02060
         MVI   0(R5),C' '                                               CMS02070
         LA    R6,1                                                     CMS02080
         B     WRITEIT                                                  CMS02090
PRINT0   LA    R5,DATABUF                                               CMS02100
         L     R6,RECLEN                                                CMS02110
WRITEIT  LR    R2,R5                                                    CMS02120
         LR    R3,R6                                                    CMS02130
         LINEWRT DATA=((R2),(R3))                                       CMS02140
         LTR   R15,R15                                                  CMS02150
         BNZ   OUTERR                                                   CMS02160
         B     READLOOP                                                 CMS02170
DREADER  C     R15,=F'12'                                               CMS02180
         BE    DONE                                                     CMS02190
         APPLMSG TEXT='CMSAWK: ERROR READING INPUT FILE'                CMS02200
         B     FAIL8                                                    CMS02210
DOPENER  APPLMSG TEXT='CMSAWK: CANNOT OPEN INPUT FILE'                  CMS02220
         B     FAIL8                                                    CMS02230
OUTERR   APPLMSG TEXT='CMSAWK: TERMINAL OUTPUT FAILED'                  CMS02240
         B     FAIL8                                                    CMS02250
FAIL24   LA    R10,24                                                   CMS02260
         B     CLEANUP                                                  CMS02270
FAIL8    LA    R10,8                                                    CMS02280
         B     CLEANUP                                                  CMS02290
DONE     SR    R10,R10                                                  CMS02300
CLEANUP  L     R0,DOPEN                                                 CMS02310
         LTR   R0,R0                                                    CMS02320
         BZ    CLOSCR                                                   CMS02330
         FSCLOSE FSCB=DFSCB                                             CMS02340
CLOSCR   L     R0,SOPEN                                                 CMS02350
         LTR   R0,R0                                                    CMS02360
         BZ    RELEASE                                                  CMS02370
         FSCLOSE FSCB=SFSCB                                             CMS02380
RELEASE  CMSRET RC=(R10)                                                CMS02390
SAVE01   DS    18F                                                      CMS02400
SFSCBI   FSCB  FORM=E,OPENTYP=READ,CACHE=YES,RECNO=0,NOREC=1            CMS02410
DFSCBI   FSCB  FORM=E,OPENTYP=READ,CACHE=YES,RECNO=0,NOREC=1            CMS02420
BLANK80  DC    CL80' '                                                  CMS02430
SFSCB    FSCB  FORM=E                                                   CMS02440
DFSCB    FSCB  FORM=E                                                   CMS02450
SCRFID   DS    CL24                                                     CMS02460
DATFID   DS    CL24                                                     CMS02470
SCRBUF   DS    CL80                                                     CMS02480
DATABUF  DS    CL80                                                     CMS02490
PATTERN  DS    CL80                                                     CMS02500
OUTBUF   DS    CL80                                                     CMS02510
SLEN     DS    F                                                        CMS02520
PATLEN   DS    F                                                        CMS02530
FIELDNO  DS    F                                                        CMS02540
RECLEN   DS    F                                                        CMS02550
SOPEN    DS    F                                                        CMS02560
DOPEN    DS    F                                                        CMS02570
FSTART   DS    10X                                                      CMS02580
FLEN     DS    10X                                                      CMS02590
PLIST    DSECT                                                          CMS02600
         DS    CL8                                                      CMS02610
SCRIPT   DS    3CL8                                                     CMS02620
DATAFILE DS    3CL8                                                     CMS02630
         REGEQU                                                         CMS02640
         END                                                            CMS02650
