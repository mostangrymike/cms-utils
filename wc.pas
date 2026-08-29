PROGRAM WC (INPUT, OUTPUT);

CONST
  Max_Command_Length = 255;
  Max_Arguments = 64;

TYPE
  Command_String = VARYING [Max_Command_Length] OF CHAR;
  File_Name = VARYING [Max_Command_Length] OF CHAR;

FUNCTION LIB$GET_FOREIGN (%DESCR Resultant_String : Command_String) : INTEGER; EXTERN;

VAR
  Show_Lines, Show_Words, Show_Bytes : BOOLEAN;
  File_Count, I, Status : INTEGER;
  Command_Length : INTEGER;
  Arguments : ARRAY [1..Max_Arguments] OF File_Name;
  Command_Line : Command_String;
  Total_Lines, Total_Words, Total_Bytes : INTEGER64;

FUNCTION White_Space (C : CHAR) : BOOLEAN;
BEGIN
  White_Space := (C = ' ') OR (C = CHR(9)) OR (C = CHR(11)) OR
                 (C = CHR(12)) OR (C = CHR(13));
END;

PROCEDURE Count_Text (VAR F : TEXT;
                      VAR Lines, Words, Bytes : INTEGER64);
VAR
  C : CHAR;
  In_Word : BOOLEAN;
BEGIN
  Lines := 0;
  Words := 0;
  Bytes := 0;
  In_Word := FALSE;

  WHILE NOT EOF(F) DO BEGIN
    WHILE NOT EOLN(F) DO BEGIN
      C := F^;
      Bytes := Bytes + 1;
      IF White_Space(C) THEN
        In_Word := FALSE
      ELSE IF NOT In_Word THEN BEGIN
        Words := Words + 1;
        In_Word := TRUE
      END;
      GET(F)
    END;
    READLN(F);
    Lines := Lines + 1;
    Bytes := Bytes + 1;
    In_Word := FALSE
  END
END;

PROCEDURE Print_Count (Lines, Words, Bytes : INTEGER64;
                       Name : File_Name);
BEGIN
  IF Show_Lines THEN
    WRITE(Lines:1, ' ');
  IF Show_Words THEN
    WRITE(Words:1, ' ');
  IF Show_Bytes THEN
    WRITE(Bytes:1, ' ');
  IF LENGTH(Name) <> 0 THEN
    WRITE(Name);
  WRITELN
END;

PROCEDURE Add_Argument (S : File_Name);
BEGIN
  IF File_Count < Max_Arguments THEN BEGIN
    File_Count := File_Count + 1;
    Arguments[File_Count] := S
  END
END;

PROCEDURE Parse_Command_Line;
VAR
  P, Start : INTEGER;
  Token : File_Name;
  Is_Option : BOOLEAN;
BEGIN
  P := 1;
  WHILE P <= Command_Length DO BEGIN
    WHILE (P <= Command_Length) AND (Command_Line[P] = ' ') DO
      P := P + 1;
    Start := P;
    WHILE (P <= Command_Length) AND (Command_Line[P] <> ' ') DO
      P := P + 1;
    IF P > Start THEN BEGIN
      Token := SUBSTR(Command_Line, Start, P - Start);
      Is_Option := (LENGTH(Token) > 1) AND (Token[1] = '-');
      IF Is_Option THEN BEGIN
        FOR I := 2 TO LENGTH(Token) DO
          CASE Token[I] OF
            'l' : Show_Lines := TRUE;
            'w' : Show_Words := TRUE;
            'c' : Show_Bytes := TRUE
          END
      END
      ELSE
        Add_Argument(Token)
    END
  END
END;

VAR
  F : TEXT;
  Lines, Words, Bytes : INTEGER64;
  Name : File_Name;

BEGIN
  Show_Lines := FALSE;
  Show_Words := FALSE;
  Show_Bytes := FALSE;
  File_Count := 0;
  Command_Line := '';
  Command_Length := 0;

  Status := LIB$GET_FOREIGN(Command_Line);
  IF (Status MOD 2) <> 0 THEN
    Command_Length := LENGTH(Command_Line)
  ELSE
    Command_Length := 0;
  Parse_Command_Line;

  IF NOT (Show_Lines OR Show_Words OR Show_Bytes) THEN BEGIN
    Show_Lines := TRUE;
    Show_Words := TRUE;
    Show_Bytes := TRUE
  END;

  Total_Lines := 0;
  Total_Words := 0;
  Total_Bytes := 0;

  IF File_Count = 0 THEN BEGIN
    Count_Text(INPUT, Lines, Words, Bytes);
    Name := '';
    Print_Count(Lines, Words, Bytes, Name)
  END
  ELSE BEGIN
    FOR I := 1 TO File_Count DO BEGIN
      RESET(F, Arguments[I]);
      Count_Text(F, Lines, Words, Bytes);
      CLOSE(F);
      Print_Count(Lines, Words, Bytes, Arguments[I]);
      Total_Lines := Total_Lines + Lines;
      Total_Words := Total_Words + Words;
      Total_Bytes := Total_Bytes + Bytes
    END;
    IF File_Count > 1 THEN BEGIN
      Name := 'total';
      Print_Count(Total_Lines, Total_Words, Total_Bytes, Name)
    END
  END
END.
