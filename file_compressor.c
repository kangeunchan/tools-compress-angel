// main.c
#include <gtk/gtk.h>
#include <zlib.h>
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

gboolean update_progress(gpointer data);
gboolean update_label(gpointer data);
gboolean update_log(gpointer data);
void append_log(GtkWidget *logView, const char *message);
gboolean on_process_complete(gpointer data);
void *processFileThread(void *arg);
char *chooseFile();
void on_compress_clicked(GtkWidget *widget, gpointer data);
void on_decompress_clicked(GtkWidget *widget, gpointer data);

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    // 메인 윈도우 생성
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "파일 압축/해제 (zlib)");
    gtk_window_set_default_size(GTK_WINDOW(window), 500, 400);
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

// 진행률 업데이트 함수
gboolean update_progress(gpointer data) {
    ThreadData *threadData = (ThreadData *)data;
    double progress = (double)threadData->totalProcessed / threadData->fileSize;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(threadData->progressBar), progress);

    clock_t currentTime = clock();
    double elapsedTime = (double)(currentTime - threadData->startTime) / CLOCKS_PER_SEC;
    double speed = threadData->totalProcessed / elapsedTime;

    char speedInfo[256];
    snprintf(speedInfo, sizeof(speedInfo), "처리 속도: %.2f 바이트/초", speed);
    gtk_label_set_text(GTK_LABEL(threadData->speedLabel), speedInfo);

    return FALSE;
}

// 레이블 업데이트 함수
gboolean update_label(gpointer data) {
    GtkLabel *label = GTK_LABEL(data);
    const gchar *message = g_object_get_data(G_OBJECT(label), "message");
    if (message) {
        gtk_label_set_text(label, message);
        g_object_set_data(G_OBJECT(label), "message", NULL);
    }
    return FALSE;
}

// 로그 업데이트 함수
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
        double compressionRatio = 100.0 * (1.0 - (double)outputFileSize / threadData->fileSize);
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
    FILE *dest = fopen(threadData->outputFile, "wb");
    if (source == NULL || dest == NULL) {
        append_log(threadData->logView, "파일을 열 수 없습니다.");
        if (source) fclose(source);
        if (dest) fclose(dest);
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

    int ret, flush;
    unsigned have;
    z_stream strm;
    unsigned char in[CHUNK];
    unsigned char out[CHUNK];
    threadData->totalProcessed = 0;
    threadData->startTime = clock();

    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;

    if (threadData->isCompress) {
        ret = deflateInit(&strm, Z_BEST_COMPRESSION);
    } else {
        ret = inflateInit(&strm);
    }

    if (ret != Z_OK) {
        append_log(threadData->logView, "스트림 초기화 실패");
        fclose(source);
        fclose(dest);
        g_idle_add(on_process_complete, threadData);
        return NULL;
    }

    do {
        strm.avail_in = fread(in, 1, CHUNK, source);
        if (ferror(source)) {
            append_log(threadData->logView, "파일 읽기 오류");
            if (threadData->isCompress)
                deflateEnd(&strm);
            else
                inflateEnd(&strm);
            fclose(source);
            fclose(dest);
            g_idle_add(on_process_complete, threadData);
            return NULL;
        }
        flush = feof(source) ? Z_FINISH : Z_NO_FLUSH;
        strm.next_in = in;

        do {
            strm.avail_out = CHUNK;
            strm.next_out = out;
            if (threadData->isCompress)
                ret = deflate(&strm, flush);
            else
                ret = inflate(&strm, flush);
            have = CHUNK - strm.avail_out;
            if (fwrite(out, 1, have, dest) != have || ferror(dest)) {
                append_log(threadData->logView, "파일 쓰기 오류");
                if (threadData->isCompress)
                    deflateEnd(&strm);
                else
                    inflateEnd(&strm);
                fclose(source);
                fclose(dest);
                g_idle_add(on_process_complete, threadData);
                return NULL;
            }

            threadData->totalProcessed += have;
            g_idle_add(update_progress, threadData);

        } while (strm.avail_out == 0);
    } while (flush != Z_FINISH);

    if (threadData->isCompress)
        deflateEnd(&strm);
    else
        inflateEnd(&strm);

    fclose(source);
    fclose(dest);

    g_idle_add(on_process_complete, threadData);

    return NULL;
}

// 파일 선택 함수 (Windows와 다른 OS 구분)
char *chooseFile() {
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
                                         NULL,
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
    GtkWidget *window = (GtkWidget *)data;
    GtkWidget *progressBar = g_object_get_data(G_OBJECT(window), "progress_bar");
    GtkWidget *statusLabel = g_object_get_data(G_OBJECT(window), "status_label");
    GtkWidget *logView = g_object_get_data(G_OBJECT(window), "log_view");
    GtkWidget *fileInfoLabel = g_object_get_data(G_OBJECT(window), "file_info_label");
    GtkWidget *speedLabel = g_object_get_data(G_OBJECT(window), "speed_label");

    char *inputFile = chooseFile();
    if (inputFile != NULL) {
        char *outputFile = g_strdup_printf("%s.gz", inputFile);

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
    GtkWidget *window = (GtkWidget *)data;
    GtkWidget *progressBar = g_object_get_data(G_OBJECT(window), "progress_bar");
    GtkWidget *statusLabel = g_object_get_data(G_OBJECT(window), "status_label");
    GtkWidget *logView = g_object_get_data(G_OBJECT(window), "log_view");
    GtkWidget *fileInfoLabel = g_object_get_data(G_OBJECT(window), "file_info_label");
    GtkWidget *speedLabel = g_object_get_data(G_OBJECT(window), "speed_label");

    char *inputFile = chooseFile();
    if (inputFile != NULL) {
        char *outputFile = g_strdup(inputFile);
        char *ext = strrchr(outputFile, '.');
        if (ext != NULL && strcmp(ext, ".gz") == 0) {
            *ext = '\0';
        } else {
            gtk_label_set_text(GTK_LABEL(statusLabel), "잘못된 파일 확장자입니다.");
            g_free(inputFile);
            g_free(outputFile);
            return;
        }

        gtk_label_set_text(GTK_LABEL(statusLabel), "해제 중...");
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
