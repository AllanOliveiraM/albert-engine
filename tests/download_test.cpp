#include "download.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <curl/curl.h>

#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

int main(int argc, char** argv) {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) return 1;
    int result = 0;
    try {
        albert::DownloadProgress progress;
        progress.stage("Teste", 2);
        progress.source("example.org");
        progress.transfer(5, 10);
        const auto status = progress.snapshot();
        if (status.stage != "Teste" || status.completed != 2 ||
            status.received != 5 || status.total != 10 || status.source != "example.org") {
            throw std::runtime_error("Progresso inconsistente");
        }
        progress.cancelled = true;
        bool cancelled = false;
        try {
            albert::httpGet("http://127.0.0.1:1", &progress);
        } catch (const std::exception& error) {
            cancelled = std::string(error.what()) == "Download cancelado";
        }
        if (!cancelled) throw std::runtime_error("Cancelamento ignorado");
        progress.cancelled = false;
        progress.stage("Nova tentativa", 0);
        if (progress.snapshot().received != 0) throw std::runtime_error("Progresso antigo preservado");
        if (!progress.snapshot().source.empty()) throw std::runtime_error("Origem antiga preservada");
        progress.transfer(1,4,albert::ProgressUnit::Items);
        if(progress.snapshot().unit!=albert::ProgressUnit::Items)throw std::runtime_error("Unidade dos blocos locais incorreta");
        progress.stage("HTTP",0);
        if(progress.snapshot().unit!=albert::ProgressUnit::Bytes)throw std::runtime_error("Unidade antiga preservada");

        // Optional local HTTP fixture exercises real transfers, HTTP failures,
        // a successful retry, and cancellation while a response is in flight.
        if (argc == 2) {
            const std::string base = argv[1];
            bool failed = false;
            try { albert::httpGet(base + "/error", &progress); }
            catch (const std::exception&) { failed = true; }
            if (!failed) throw std::runtime_error("HTTP 503 aceito");
            failed = false;
            try { albert::httpGet(base + "/truncated", &progress); }
            catch (const std::exception&) { failed = true; }
            if (!failed) throw std::runtime_error("Transferencia incompleta aceita");
            progress.stage("Nova tentativa", 0);
            if (albert::httpGet(base + "/ok", &progress) != std::string(4096, 'x')) {
                throw std::runtime_error("Nova tentativa retornou dados incorretos");
            }
            if (progress.snapshot().received != 4096 || progress.snapshot().total != 4096) {
                throw std::runtime_error("Contagem de bytes incorreta");
            }
            if (progress.snapshot().source != base.substr(base.find("://")+3)) {
                throw std::runtime_error("Origem da transferencia incorreta");
            }
            failed=false;
            try{albert::httpGet(base+"/ok",&progress,{1024,5});}catch(const std::exception& e){failed=std::string(e.what()).find("size limit")!=std::string::npos;}
            if(!failed)throw std::runtime_error("Limite de resposta HTTP ignorado");
            progress.stage("Cancelando transferencia", 0);
            auto pending = std::async(std::launch::async, [&] {
                try { albert::httpGet(base + "/slow", &progress); }
                catch (const std::exception& error) {
                    return std::string(error.what()) == "Download cancelado";
                }
                return false;
            });
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (progress.snapshot().received == 0 && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            progress.cancelled = true;
            if (!pending.get()) throw std::runtime_error("Transferencia nao cancelada");
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    curl_global_cleanup();
    return result;
}
