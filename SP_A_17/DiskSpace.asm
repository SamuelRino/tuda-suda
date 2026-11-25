; Практическая работа №6 «Интерфейс прикладного программирования Win32 API».
; Студент: Рункин. Группа: не указана. Вариант 12.
; Назначение: получить параметры дискового пространства для корня, указанного в .data.
; BOOL GetDiskFreeSpaceA(LPCSTR root, LPDWORD sectorsPerCluster,
;     LPDWORD bytesPerSector, LPDWORD freeClusters, LPDWORD totalClusters);

.386
.model flat, stdcall
option casemap:none

GetDiskFreeSpaceA PROTO :DWORD, :DWORD, :DWORD, :DWORD, :DWORD
GetLastError PROTO
ExitProcess PROTO :DWORD
MessageBoxA PROTO :DWORD, :DWORD, :DWORD, :DWORD
wsprintfA PROTO C :DWORD, :DWORD, :VARARG

.data
rootPath db 'C:\',0
caption db 'Practice 06 - GetDiskFreeSpaceA (MASM)',0
results db 'Root: %s',13,10
        db 'Sectors per cluster: %lu',13,10
        db 'Bytes per sector: %lu',13,10
        db 'Free clusters: %lu',13,10
        db 'Total clusters: %lu',0
failure db 'GetDiskFreeSpaceA failed. Win32 error: %lu',0

.data?
sectorsPerCluster dd ?
bytesPerSector dd ?
freeClusters dd ?
totalClusters dd ?
messageBuffer db 256 dup (?)

.code
AsmMain PROC
    invoke GetDiskFreeSpaceA, ADDR rootPath, ADDR sectorsPerCluster, ADDR bytesPerSector, \
        ADDR freeClusters, ADDR totalClusters
    test eax, eax
    jz apiError

    invoke wsprintfA, ADDR messageBuffer, ADDR results, ADDR rootPath, \
        sectorsPerCluster, bytesPerSector, freeClusters, totalClusters
    invoke MessageBoxA, 0, ADDR messageBuffer, ADDR caption, 40h
    invoke ExitProcess, 0

apiError:
    invoke GetLastError
    invoke wsprintfA, ADDR messageBuffer, ADDR failure, eax
    invoke MessageBoxA, 0, ADDR messageBuffer, ADDR caption, 10h
    invoke ExitProcess, 1
AsmMain ENDP
END
