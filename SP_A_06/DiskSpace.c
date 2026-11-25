/*
 * Практическая работа №6 «Интерфейс прикладного программирования Win32 API».
 * Студент: Рункин. Группа: не указана. Вариант 12.
 * Назначение: получить параметры дискового пространства для корня из командной строки.
 * BOOL GetDiskFreeSpaceA(LPCSTR root, LPDWORD sectorsPerCluster,
 *     LPDWORD bytesPerSector, LPDWORD freeClusters, LPDWORD totalClusters);
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static int is_root(const char *path)
{
    size_t length = strlen(path);
    if (length == 3 && path[1] == ':' && path[2] == '\\') {
        char letter = path[0];
        return (letter >= 'A' && letter <= 'Z') || (letter >= 'a' && letter <= 'z');
    }

    if (length < 6 || path[0] != '\\' || path[1] != '\\' || path[2] == '\\') {
        return 0;
    }
    /* Для UNC допустим ровно корень \\сервер\ресурс\ с конечной обратной косой чертой. */
    const char *share = strchr(path + 2, '\\');
    return share != NULL && share[1] != '\\' && strchr(share + 1, '\\') == path + length - 1;
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: DiskSpaceC.exe <drive root, e.g. C:\\>\n");
        return 2;
    }
    if (!is_root(argv[1])) {
        fprintf(stderr, "Invalid root. Use C:\\ or \\\\server\\share\\.\n");
        return 2;
    }

    DWORD sectorsPerCluster, bytesPerSector, freeClusters, totalClusters;
    if (!GetDiskFreeSpaceA(argv[1], &sectorsPerCluster, &bytesPerSector,
        &freeClusters, &totalClusters)) {
        DWORD error = GetLastError();
        fprintf(stderr, "GetDiskFreeSpaceA failed. Win32 error: %lu\n", error);
        return 1;
    }

    printf("Root: %s\n", argv[1]);
    printf("Sectors per cluster: %lu\n", sectorsPerCluster);
    printf("Bytes per sector: %lu\n", bytesPerSector);
    printf("Free clusters: %lu\n", freeClusters);
    printf("Total clusters: %lu\n", totalClusters);
    return 0;
}
