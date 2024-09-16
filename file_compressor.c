#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

#define CHUNK 16384
#define MAX_TREE_HT 100

typedef struct {
    char *inputFile;
    char *outputFile;
    GtkWidget *progressBar;
    GtkWidget *statusLabel;
    GtkWidget *logView;
    GtkWidget *fileInfoLabel;
    GtkWidget *speedLabel;
    gboolean isCompress;
    long fileSize;
    long totalProcessed;
    clock_t startTime;
} ThreadData;

// 하프만 트리 노드 정의
struct MinHeapNode {
    unsigned char data;
    unsigned freq;
    struct MinHeapNode *left, *right;
};

// 하프만 힙 정의
struct MinHeap {
    unsigned size;
    unsigned capacity;
    struct MinHeapNode **array;
};

// 하프만 코딩과 관련된 함수들
struct MinHeapNode* newNode(unsigned char data, unsigned freq) {
    struct MinHeapNode* temp = (struct MinHeapNode*) malloc(sizeof(struct MinHeapNode));
    temp->left = temp->right = NULL;
    temp->data = data;
    temp->freq = freq;
    return temp;
}

struct MinHeap* createMinHeap(unsigned capacity) {
    struct MinHeap* minHeap = (struct MinHeap*) malloc(sizeof(struct MinHeap));
    minHeap->size = 0;
    minHeap->capacity = capacity;
    minHeap->array = (struct MinHeapNode**) malloc(minHeap->capacity * sizeof(struct MinHeapNode*));
    return minHeap;
}

void swapMinHeapNode(struct MinHeapNode** a, struct MinHeapNode** b) {
    struct MinHeapNode* t = *a;
    *a = *b;
    *b = t;
}

void minHeapify(struct MinHeap* minHeap, int idx) {
    int smallest = idx;
    int left = 2 * idx + 1;
    int right = 2 * idx + 2;

    if (left < minHeap->size && minHeap->array[left]->freq < minHeap->array[smallest]->freq)
        smallest = left;

    if (right < minHeap->size && minHeap->array[right]->freq < minHeap->array[smallest]->freq)
        smallest = right;

    if (smallest != idx) {
        swapMinHeapNode(&minHeap->array[smallest], &minHeap->array[idx]);
        minHeapify(minHeap, smallest);
    }
}

struct MinHeapNode* extractMin(struct MinHeap* minHeap) {
    struct MinHeapNode* temp = minHeap->array[0];
    minHeap->array[0] = minHeap->array[minHeap->size - 1];
    --minHeap->size;
    minHeapify(minHeap, 0);
    return temp;
}

void insertMinHeap(struct MinHeap* minHeap, struct MinHeapNode* minHeapNode) {
    ++minHeap->size;
    int i = minHeap->size - 1;
    while (i && minHeapNode->freq < minHeap->array[(i - 1) / 2]->freq) {
        minHeap->array[i] = minHeap->array[(i - 1) / 2];
        i = (i - 1) / 2;
    }
    minHeap->array[i] = minHeapNode;
}

void buildMinHeap(struct MinHeap* minHeap) {
    int n = minHeap->size - 1;
    for (int i = (n - 1) / 2; i >= 0; --i)
        minHeapify(minHeap, i);
}

struct MinHeap* createAndBuildMinHeap(unsigned char data[], unsigned freq[], int size) {
    struct MinHeap* minHeap = createMinHeap(size);
    for (int i = 0; i < size; ++i)
        minHeap->array[i] = newNode(data[i], freq[i]);
    minHeap->size = size;
    buildMinHeap(minHeap);
    return minHeap;
}

struct MinHeapNode* buildHuffmanTree(unsigned char data[], unsigned freq[], int size) {
    struct MinHeapNode *left, *right, *top;
    struct MinHeap* minHeap = createAndBuildMinHeap(data, freq, size);
    while (minHeap->size != 1) {
        left = extractMin(minHeap);
        right = extractMin(minHeap);
        top = newNode('$', left->freq + right->freq);
        top->left = left;
        top->right = right;
        insertMinHeap(minHeap, top);
    }
    struct MinHeapNode* root = extractMin(minHeap);
    free(minHeap->array);
    free(minHeap);
    return root;
}

void printCodes(struct MinHeapNode* root, char **codes, char arr[], int top) {
    if (root->left) {
        arr[top] = '0';
        printCodes(root->left, codes, arr, top + 1);
    }
    if (root->right) {
        arr[top] = '1';
        printCodes(root->right, codes, arr, top + 1);
    }
    if (!(root->left) && !(root->right)) {
        arr[top] = '\0';
        codes[root->data] = strdup(arr);
    }
}

char** generateHuffmanCodes(struct MinHeapNode* root) {
    char** codes = (char**) malloc(256 * sizeof(char*));
    for(int i = 0; i < 256; i++) codes[i] = NULL;
    char arr[MAX_TREE_HT];
    printCodes(root, codes, arr, 0);
    return codes;
}

void freeHuffmanTree(struct MinHeapNode* root) {
    if (root == NULL) return;
    freeHuffmanTree(root->left);
    freeHuffmanTree(root->right);
    free(root);
}

// 하프만 압축 함수 (비트 패킹 적용)
size_t huffmanEncode(const unsigned char* input, size_t size, unsigned char* output, char** codes) {
    size_t outIdx = 0;
    unsigned char currentByte = 0;
    int bitPos = 7; // 현재 바이트의 비트 위치 (7부터 0까지)

    for (size_t i = 0; i < size; i++) {
        char* code = codes[input[i]];
        if (code == NULL) continue; // 빈도수가 0인 경우
        size_t codeLen = strlen(code);
        for (size_t j = 0; j < codeLen; j++) {
            if (code[j] == '1') {
                currentByte |= (1 << bitPos);
            }
            bitPos--;
            if (bitPos < 0) {
                output[outIdx++] = currentByte;
                currentByte = 0;
                bitPos = 7;
            }
        }
    }

    // 마지막에 남은 비트가 있으면 패딩하여 저장
    if (bitPos != 7) {
        output[outIdx++] = currentByte;
    }

    return outIdx;
}

// 하프만 압축 해제 함수 (비트 언패킹 적용)
unsigned char* huffmanDecode(const unsigned char* encodedData, size_t encodedSize, struct MinHeapNode* root, size_t* decodedSize) {
    unsigned char* decoded = (unsigned char*)malloc(encodedSize * 8); // 임시 버퍼
    if (!decoded) {
        fprintf(stderr, "메모리 할당 실패 (Huffman Decode)\n");
        exit(1);
    }

    size_t outIdx = 0;
    struct MinHeapNode* current = root;

    for (size_t i = 0; i < encodedSize; i++) {
        unsigned char byte = encodedData[i];
        for (int bit = 7; bit >= 0; bit--) {
            int bitValue = (byte >> bit) & 1;
            if (bitValue == 0) {
                current = current->left;
            } else {
                current = current->right;
            }

            if (!current->left && !current->right) {
                decoded[outIdx++] = current->data;
                current = root;
            }
        }
    }

    *decodedSize = outIdx;
    return decoded;
}

// 하프만 압축을 위한 빈도 계산
void calculateFrequency(const unsigned char* input, size_t size, unsigned freq[]) {
    memset(freq, 0, 256 * sizeof(unsigned));
    for (size_t i = 0; i < size; i++) {
        freq[input[i]]++;
    }
}

// 새로운 압축 알고리즘 정의 (RLE 제거 및 하프만 코딩 적용)
void advancedCompression(unsigned char* data, size_t size, unsigned char** compressedData, size_t* compressedSize) {
    // 하프만 코딩만 적용하여 모든 파일에 대해 일관된 압축 수행
    // 1. 하프만 코딩을 위한 빈도 계산
    unsigned freq[256];
    calculateFrequency(data, size, freq);

    // 2. 하프만 트리 구축
    unsigned unique = 0;
    unsigned char uniqueChars[256];
    unsigned uniqueFreqArr[256];
    for(int i = 0; i < 256; i++) {
        if(freq[i] > 0){
            uniqueChars[unique] = i;
            uniqueFreqArr[unique] = freq[i];
            unique++;
        }
    }

    if(unique == 0){
        *compressedData = NULL;
        *compressedSize = 0;
        return;
    }

    struct MinHeapNode* root = buildHuffmanTree(uniqueChars, uniqueFreqArr, unique);

    // 3. 하프만 코드 생성
    char** codes = generateHuffmanCodes(root);

    // 4. 하프만 인코딩 (비트 패킹 적용)
    unsigned char* huffmanBuffer = (unsigned char*)malloc(size); // 최대 size 바이트
    if (!huffmanBuffer) {
        fprintf(stderr, "메모리 할당 실패 (Huffman)\n");
        freeHuffmanTree(root);
        exit(1);
    }

    size_t huffmanSize = huffmanEncode(data, size, huffmanBuffer, codes);

    // 5. 압축 데이터 저장
    // 압축된 데이터는 다음과 같은 형식으로 저장됩니다:
    // [고유 문자 수][문자][빈도]... [인코딩된 비트]
    size_t headerSize = 1 + unique * (1 + sizeof(unsigned));
    unsigned char* finalBuffer = (unsigned char*)malloc(headerSize + huffmanSize);
    if (!finalBuffer) {
        fprintf(stderr, "메모리 할당 실패 (Final Buffer)\n");
        free(huffmanBuffer);
        freeHuffmanTree(root);
        for(int i = 0; i < 256; i++) if(codes[i]) free(codes[i]);
        free(codes);
        exit(1);
    }

    // 고유 문자 수 저장
    finalBuffer[0] = (unsigned char)unique;
    size_t idx = 1;
    for(unsigned i = 0; i < unique; i++) {
        finalBuffer[idx++] = uniqueChars[i];
        unsigned freqVal = uniqueFreqArr[i];
        memcpy(&finalBuffer[idx], &freqVal, sizeof(unsigned));
        idx += sizeof(unsigned);
    }

    // 인코딩된 데이터 저장
    memcpy(&finalBuffer[idx], huffmanBuffer, huffmanSize);
    idx += huffmanSize;

    *compressedData = finalBuffer;
    *compressedSize = idx;

    // 메모리 해제
    free(huffmanBuffer);
    freeHuffmanTree(root);
    for(int i = 0; i < 256; i++) if(codes[i]) free(codes[i]);
    free(codes);
}

// 하프만 압축 해제 함수 (비트 언패킹 적용)
void advancedDecompression(unsigned char* compressed_data, size_t compressed_size, unsigned char** decompressedData, size_t* decompressedSize) {
    if (compressed_size < 1) {
        *decompressedData = NULL;
        *decompressedSize = 0;
        return;
    }

    // 1. 고유 문자 수 읽기
    unsigned unique = compressed_data[0];
    size_t idx = 1;

    if (compressed_size < 1 + unique * (1 + sizeof(unsigned))) {
        // 유효하지 않은 압축 데이터
        *decompressedData = NULL;
        *decompressedSize = 0;
        return;
    }

    unsigned char uniqueChars[256];
    unsigned uniqueFreqArr[256];
    for(unsigned i = 0; i < unique; i++) {
        uniqueChars[i] = compressed_data[idx++];
        memcpy(&uniqueFreqArr[i], &compressed_data[idx], sizeof(unsigned));
        idx += sizeof(unsigned);
    }

    // 2. 하프만 트리 재구축
    struct MinHeapNode* root = buildHuffmanTree(uniqueChars, uniqueFreqArr, unique);

    // 3. 인코딩된 데이터 읽기
    size_t encodedDataSize = compressed_size - idx;
    unsigned char* encodedData = (unsigned char*)malloc(encodedDataSize);
    if (!encodedData) {
        fprintf(stderr, "메모리 할당 실패 (Encoded Data)\n");
        freeHuffmanTree(root);
        exit(1);
    }
    memcpy(encodedData, &compressed_data[idx], encodedDataSize);

    // 4. 하프만 디코딩 (비트 언패킹 적용)
    size_t decodedSizeHuffman = 0;
    unsigned char* huffmanDecoded = huffmanDecode(encodedData, encodedDataSize, root, &decodedSizeHuffman);
    free(encodedData);
    freeHuffmanTree(root);

    // 5. 원본 데이터 복원
    *decompressedData = huffmanDecoded;
    *decompressedSize = decodedSizeHuffman;
}

// CSS 스타일 정의
const char *css_style = "\
    window {\
        background-color: #FFFFFF;\
    }\
    button {\
        background-color: #0055FF;\
        color: #FFFFFF;\
        border-radius: 8px;\
        padding: 12px 20px;\
        font-size: 16px;\
        font-weight: bold;\
        border: none;\
        cursor: pointer;\
        transition: background-color 0.3s ease;\
    }\
    button:hover {\
        background-color: #0044CC;\
    }\
    progressbar trough {\
        background-color: #E0E0E0;\
        border-radius: 8px;\
        min-height: 18px;\
    }\
    progressbar progress {\
        background-color: #0055FF;\
        border-radius: 8px;\
    }\
    label {\
        color: #333333;\
        font-size: 14px;\
    }\
    #status_label {\
        font-size: 14px;\
        color: #666666;\
        margin-top: 10px;\
    }\
";

// 진행률 업데이트 함수
gboolean update_progress(gpointer data) {
    ThreadData *threadData = (ThreadData *)data;
    double progress = (double)threadData->totalProcessed / threadData->fileSize;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(threadData->progressBar), progress);

    clock_t currentTime = clock();
    double elapsedTime = (double)(currentTime - threadData->startTime) / CLOCKS_PER_SEC;
    double speed = elapsedTime > 0 ? threadData->totalProcessed / elapsedTime : 0;

    char speedInfo[256];
    snprintf(speedInfo, sizeof(speedInfo), "처리 속도: %.2f 바이트/초", speed);
    gtk_label_set_text(GTK_LABEL(threadData->speedLabel), speedInfo);

    // 계속해서 업데이트하기 위해 TRUE 반환
    return TRUE;
}

// 레이블 및 로그 업데이트 함수
gboolean update_label(gpointer data) {
    GtkLabel *label = GTK_LABEL(data);
    const gchar *message = g_object_get_data(G_OBJECT(label), "message");
    if (message) {
        gtk_label_set_text(label, message);
        g_object_set_data(G_OBJECT(label), "message", NULL);
    }
    return FALSE;
}

gboolean update_log(gpointer data) {
    GtkTextView *logView = GTK_TEXT_VIEW(data);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(logView);
    const gchar *message = g_object_get_data(G_OBJECT(logView), "message");
    if (message) {
        GtkTextIter iter;
        gtk_text_buffer_get_end_iter(buffer, &iter);
        gtk_text_buffer_insert(buffer, &iter, message, -1);
        gtk_text_buffer_insert(buffer, &iter, "\n", -1);
        g_object_set_data(G_OBJECT(logView), "message", NULL);
    }
    return FALSE;
}

// 로그에 메시지 추가 함수
void append_log(GtkWidget *logView, const char *message) {
    g_object_set_data_full(G_OBJECT(logView), "message", g_strdup(message), g_free);
    g_idle_add(update_log, logView);
}

// 작업 완료 시 호출되는 함수
gboolean on_process_complete(gpointer data) {
    ThreadData *threadData = (ThreadData *)data;

    // 진행 바를 100%로 업데이트
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(threadData->progressBar), 1.0);

    // 로그에 "작업 완료!" 추가
    append_log(threadData->logView, "작업 완료!");

    // 압축률 계산 및 표시
    struct stat st;
    if (stat(threadData->outputFile, &st) == 0) {
        long outputFileSize = st.st_size;
        double compressionRatio = 100.0 * (1.0 - ((double)outputFileSize / threadData->fileSize));
        char sizeInfo[256];
        snprintf(sizeInfo, sizeof(sizeInfo), "원본 크기: %ld 바이트\n출력 크기: %ld 바이트\n압축률: %.2f%%",
                 threadData->fileSize, outputFileSize, compressionRatio);
        gtk_label_set_text(GTK_LABEL(threadData->speedLabel), sizeInfo);
    }

    // 상태 레이블 업데이트
    gtk_label_set_text(GTK_LABEL(threadData->statusLabel), "작업이 완료되었습니다.");

    // 메모리 해제
    g_free(threadData->inputFile);
    g_free(threadData->outputFile);
    g_free(threadData);

    return FALSE;
}

// 파일 처리 스레드 함수
void *processFileThread(void *arg) {
    ThreadData *threadData = (ThreadData *)arg;
    FILE *source = fopen(threadData->inputFile, "rb");
    if (source == NULL) {
        append_log(threadData->logView, "파일을 열 수 없습니다.");
        g_idle_add(on_process_complete, threadData);
        return NULL;
    }

    fseek(source, 0, SEEK_END);
    threadData->fileSize = ftell(source);
    fseek(source, 0, SEEK_SET);

    char fileInfo[512];
    snprintf(fileInfo, sizeof(fileInfo), "파일명: %s\n출력 파일명: %s\n파일 크기: %ld 바이트\n",
             threadData->inputFile, threadData->outputFile, threadData->fileSize);
    g_object_set_data_full(G_OBJECT(threadData->fileInfoLabel), "message", g_strdup(fileInfo), g_free);
    g_idle_add(update_label, threadData->fileInfoLabel);

    threadData->totalProcessed = 0;
    threadData->startTime = clock();

    // 파일 전체를 메모리에 로드
    unsigned char *inBuf = (unsigned char *)malloc(threadData->fileSize);
    if (inBuf == NULL) {
        append_log(threadData->logView, "메모리 할당 실패");
        fclose(source);
        g_idle_add(on_process_complete, threadData);
        return NULL;
    }

    if (fread(inBuf, 1, threadData->fileSize, source) != threadData->fileSize) {
        append_log(threadData->logView, "파일 읽기 오류");
        fclose(source);
        free(inBuf);
        g_idle_add(on_process_complete, threadData);
        return NULL;
    }
    fclose(source);

    unsigned char *compressedData = NULL;
    size_t compressedSize = 0;
    unsigned char *decompressedData = NULL;
    size_t decompressedSize = 0;

    // 압축 또는 해제
    if (threadData->isCompress) {
        advancedCompression(inBuf, threadData->fileSize, &compressedData, &compressedSize);
        free(inBuf); // 원본 버퍼 해제
        threadData->totalProcessed = threadData->fileSize; // 압축 처리 완료
    } else {
        advancedDecompression(inBuf, threadData->fileSize, &decompressedData, &decompressedSize);
        free(inBuf); // 압축된 데이터 해제
        threadData->totalProcessed = threadData->fileSize; // 해제 처리 완료
    }

    // 출력 파일 열기
    FILE *dest = fopen(threadData->outputFile, "wb");
    if (dest == NULL) {
        append_log(threadData->logView, "출력 파일을 열 수 없습니다.");
        if (threadData->isCompress && compressedData) free(compressedData);
        if (!threadData->isCompress && decompressedData) free(decompressedData);
        g_idle_add(on_process_complete, threadData);
        return NULL;
    }

    // 압축된 데이터 쓰기
    if (threadData->isCompress) {
        if (compressedData && compressedSize > 0) {
            if (fwrite(compressedData, 1, compressedSize, dest) != compressedSize) {
                append_log(threadData->logView, "파일 쓰기 오류");
                fclose(dest);
                free(compressedData);
                g_idle_add(on_process_complete, threadData);
                return NULL;
            }
            free(compressedData);
        } else {
            append_log(threadData->logView, "압축된 데이터가 없습니다.");
        }
    } else {
        if (decompressedData && decompressedSize > 0) {
            if (fwrite(decompressedData, 1, decompressedSize, dest) != decompressedSize) {
                append_log(threadData->logView, "파일 쓰기 오류");
                fclose(dest);
                free(decompressedData);
                g_idle_add(on_process_complete, threadData);
                return NULL;
            }
            free(decompressedData);
        } else {
            append_log(threadData->logView, "해제된 데이터가 없습니다.");
        }
    }

    fclose(dest);
    g_idle_add(on_process_complete, threadData);
    return NULL;
}

// 파일 선택 함수 (Windows와 다른 OS 구분)
char *chooseFile(GtkWindow *parent) {
#ifdef _WIN32
    OPENFILENAME ofn;
    char szFile[MAX_PATH] = "";
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    if (GetOpenFileName(&ofn)) {
        return g_strdup(szFile);
    } else {
        return NULL;
    }
#else
    GtkWidget *dialog;
    dialog = gtk_file_chooser_dialog_new("파일 선택",
                                         parent,
                                         GTK_FILE_CHOOSER_ACTION_OPEN,
                                         "_취소", GTK_RESPONSE_CANCEL,
                                         "_열기", GTK_RESPONSE_ACCEPT,
                                         NULL);

    char *filename = NULL;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
        filename = gtk_file_chooser_get_filename(chooser);
    }
    gtk_widget_destroy(dialog);
    return filename;
#endif
}

// 압축 버튼 클릭 시 호출되는 함수
void on_compress_clicked(GtkWidget *widget, gpointer data) {
    GtkWidget *window = GTK_WIDGET(data);
    GtkWidget *progressBar = GTK_WIDGET(g_object_get_data(G_OBJECT(window), "progress_bar"));
    GtkWidget *statusLabel = GTK_WIDGET(g_object_get_data(G_OBJECT(window), "status_label"));
    GtkWidget *logView = GTK_WIDGET(g_object_get_data(G_OBJECT(window), "log_view"));
    GtkWidget *fileInfoLabel = GTK_WIDGET(g_object_get_data(G_OBJECT(window), "file_info_label"));
    GtkWidget *speedLabel = GTK_WIDGET(g_object_get_data(G_OBJECT(window), "speed_label"));

    char *inputFile = chooseFile(GTK_WINDOW(window));
    if (inputFile != NULL) {
        char *outputFile = g_strdup_printf("%s.adv", inputFile);

        gtk_label_set_text(GTK_LABEL(statusLabel), "압축 중...");
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progressBar), 0.0);

        ThreadData *threadData = g_new(ThreadData, 1);
        threadData->inputFile = inputFile;
        threadData->outputFile = outputFile;
        threadData->progressBar = progressBar;
        threadData->statusLabel = statusLabel;
        threadData->logView = logView;
        threadData->fileInfoLabel = fileInfoLabel;
        threadData->speedLabel = speedLabel;
        threadData->isCompress = TRUE;

        pthread_t thread;
        pthread_create(&thread, NULL, processFileThread, threadData);
        pthread_detach(thread);
    }
}

// 해제 버튼 클릭 시 호출되는 함수
void on_decompress_clicked(GtkWidget *widget, gpointer data) {
    GtkWidget *window = GTK_WIDGET(data);
    GtkWidget *progressBar = GTK_WIDGET(g_object_get_data(G_OBJECT(window), "progress_bar"));
    GtkWidget *statusLabel = GTK_WIDGET(g_object_get_data(G_OBJECT(window), "status_label"));
    GtkWidget *logView = GTK_WIDGET(g_object_get_data(G_OBJECT(window), "log_view"));
    GtkWidget *fileInfoLabel = GTK_WIDGET(g_object_get_data(G_OBJECT(window), "file_info_label"));
    GtkWidget *speedLabel = GTK_WIDGET(g_object_get_data(G_OBJECT(window), "speed_label"));

    char *inputFile = chooseFile(GTK_WINDOW(window));
    if (inputFile != NULL) {
        // ".adv" 확장자가 있는지 확인
        size_t len = strlen(inputFile);
        if (len < 5 || strcmp(&inputFile[len - 4], ".adv") != 0) {
            append_log(logView, "유효한 압축 파일(.adv)이 아닙니다.");
            g_free(inputFile);
            return;
        }

        // ".adv" 확장자를 제거하여 원본 파일명 생성
        char *outputFile = g_strdup_printf("%.*s", (int)(len - 4), inputFile);

        gtk_label_set_text(GTK_LABEL(statusLabel), "압축 해제 중...");
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progressBar), 0.0);

        ThreadData *threadData = g_new(ThreadData, 1);
        threadData->inputFile = inputFile;
        threadData->outputFile = outputFile;
        threadData->progressBar = progressBar;
        threadData->statusLabel = statusLabel;
        threadData->logView = logView;
        threadData->fileInfoLabel = fileInfoLabel;
        threadData->speedLabel = speedLabel;
        threadData->isCompress = FALSE;

        pthread_t thread;
        pthread_create(&thread, NULL, processFileThread, threadData);
        pthread_detach(thread);
    }
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    // 메인 윈도우 생성
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "파일 압축/해제 (하프만 코딩)");
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 500);
    gtk_container_set_border_width(GTK_CONTAINER(window), 20);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    // CSS 스타일 적용
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, css_style, -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                              GTK_STYLE_PROVIDER(provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    // 메인 컨테이너 생성
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_container_add(GTK_CONTAINER(window), box);

    // 버튼 박스 생성
    GtkWidget *buttonBox = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(buttonBox), GTK_BUTTONBOX_CENTER);
    gtk_box_set_spacing(GTK_BOX(buttonBox), 20);
    gtk_box_pack_start(GTK_BOX(box), buttonBox, FALSE, FALSE, 0);

    // 압축 버튼 생성
    GtkWidget *compressButton = gtk_button_new_with_label("파일 압축");
    g_signal_connect(compressButton, "clicked", G_CALLBACK(on_compress_clicked), window);
    gtk_container_add(GTK_CONTAINER(buttonBox), compressButton);

    // 해제 버튼 생성
    GtkWidget *decompressButton = gtk_button_new_with_label("파일 해제");
    g_signal_connect(decompressButton, "clicked", G_CALLBACK(on_decompress_clicked), window);
    gtk_container_add(GTK_CONTAINER(buttonBox), decompressButton);

    // 진행 바 생성
    GtkWidget *progressBar = gtk_progress_bar_new();
    gtk_box_pack_start(GTK_BOX(box), progressBar, FALSE, FALSE, 0);

    // 파일 정보 레이블 생성
    GtkWidget *fileInfoLabel = gtk_label_new("파일 정보: ");
    gtk_box_pack_start(GTK_BOX(box), fileInfoLabel, FALSE, FALSE, 0);

    // 속도 정보 레이블 생성
    GtkWidget *speedLabel = gtk_label_new("속도 정보: ");
    gtk_box_pack_start(GTK_BOX(box), speedLabel, FALSE, FALSE, 0);

    // 로그 뷰 생성
    GtkWidget *logView = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(logView), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(logView), FALSE);
    GtkWidget *scrolledWindow = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledWindow),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scrolledWindow), logView);
    gtk_box_pack_start(GTK_BOX(box), scrolledWindow, TRUE, TRUE, 0);

    // 상태 레이블 생성
    GtkWidget *statusLabel = gtk_label_new("파일을 선택하여 압축 또는 해제를 시작하세요.");
    gtk_widget_set_name(statusLabel, "status_label");
    gtk_label_set_justify(GTK_LABEL(statusLabel), GTK_JUSTIFY_CENTER);
    gtk_box_pack_start(GTK_BOX(box), statusLabel, FALSE, FALSE, 0);

    // 위젯들을 윈도우의 데이터에 저장
    g_object_set_data(G_OBJECT(window), "progress_bar", progressBar);
    g_object_set_data(G_OBJECT(window), "status_label", statusLabel);
    g_object_set_data(G_OBJECT(window), "log_view", logView);
    g_object_set_data(G_OBJECT(window), "file_info_label", fileInfoLabel);
    g_object_set_data(G_OBJECT(window), "speed_label", speedLabel);

    gtk_widget_show_all(window);

    gtk_main();

    return 0;
}
