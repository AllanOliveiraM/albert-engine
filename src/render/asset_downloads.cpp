#include "asset_downloads.hpp"
#include "platform.hpp"
#include "sha256.hpp"
#include <algorithm>
#include <fstream>

namespace fs = std::filesystem;
#ifndef ALBERT_ASSET_BASE_URL
#define ALBERT_ASSET_BASE_URL "https://github.com/AllanOliveiraM/albert-engine/releases/download"
#endif

std::string albert::AssetDownloads::defaultBaseUrl(){
    if(const auto value=environmentPath("ALBERT_ASSET_BASE_URL");!value.empty())return value.string();
    return ALBERT_ASSET_BASE_URL;
}

albert::AssetDownloads::AssetDownloads(AssetCatalog catalog,Options options):catalog_(std::move(catalog)),options_(std::move(options)){
    if(options_.baseUrl.empty())options_.baseUrl=defaultBaseUrl();
    while(!options_.baseUrl.empty()&&options_.baseUrl.back()=='/')options_.baseUrl.pop_back();
    if(options_.baseUrl.empty()||options_.backoff.empty())throw std::invalid_argument("Invalid asset download options");
    for(const auto& c:catalog_.containers)items_.push_back({c.file,c.group,c.level,c.essential,false,false,c.bytes,0});
}

albert::AssetDownloads::~AssetDownloads(){
    {std::lock_guard lock(mutex_);stop_=true;if(current_)current_->cancelled=true;}
    wake_.notify_all();if(worker_.joinable())worker_.join();
}

void albert::AssetDownloads::start(){if(!worker_.joinable())worker_=std::thread([this]{run();});}

void albert::AssetDownloads::prioritize(std::string group){
    std::lock_guard lock(mutex_);focus_=std::move(group);
    // A resumable transfer can yield to the focused body at no cost; never the essential one.
    if(active_>=0&&current_&&!items_[static_cast<size_t>(active_)].essential&&items_[static_cast<size_t>(active_)].group!=focus_&&next()!=active_){preempted_=true;current_->cancelled=true;}
}

void albert::AssetDownloads::retry(){
    {std::lock_guard lock(mutex_);retryNow_=true;failures_=0;error_.clear();}
    wake_.notify_all();
}

int albert::AssetDownloads::next() const{
    int best=-1;
    for(int i=0;i<static_cast<int>(items_.size());++i){const auto& item=items_[static_cast<size_t>(i)];
        if(item.done)continue;
        if(item.essential)return i;
        if(!focus_.empty()&&item.group==focus_&&(best<0||items_[static_cast<size_t>(best)].group!=focus_||item.level<items_[static_cast<size_t>(best)].level))best=i;
        else if(best<0)best=i;
    }
    return best;
}

fs::path albert::AssetDownloads::destination() const{
    for(const auto& directory:assetContainerDirectories()){
        std::error_code error;fs::create_directories(directory,error);if(error)continue;
        const auto probe=directory/".write-test";std::ofstream(probe,std::ios::binary)<<"albert";
        if(fs::file_size(probe,error)==6&&!error){fs::remove(probe,error);return directory;}
    }
    throw std::runtime_error("Nenhuma pasta gravavel para os dados visuais");
}

void albert::AssetDownloads::run(){backgroundPriority();
    // Containers on disk join the library without a new download once their
    // SHA-256 is known good; a size match alone is not enough.
    for(size_t i=0;i<items_.size();++i)
        for(const auto& directory:assetContainerDirectories()){
            std::error_code error;const auto path=directory/items_[i].file;const auto& expected=catalog_.containers[i];
            if(!fs::is_regular_file(path,error)||fs::file_size(path,error)!=items_[i].bytes)continue;
            try{
                if(!trustedContainer(path,expected)){
                    const auto check=std::make_shared<DownloadProgress>();{std::lock_guard lock(mutex_);if(stop_)return;current_=check;} // Cancellable.
                    const bool good=fileSha256(path,check.get())==expected.sha256;
                    {std::lock_guard lock(mutex_);current_.reset();}
                    if(!good)throw std::runtime_error("SHA-256 divergente");
                    recordVerifiedContainer(path,expected);
                }
                installVisualAssetContainer(path);std::lock_guard lock(mutex_);items_[i].done=true;items_[i].received=items_[i].bytes;items_[i].finished=finished_++;
            }catch(const std::exception&){
                {std::lock_guard lock(mutex_);current_.reset();if(stop_)return;}
                fs::remove(path,error);fs::remove(fs::path(path)+=".verified",error); // Fetched again below.
            }
            break;
        }
    while(true){
        size_t index=0;Item item;std::shared_ptr<DownloadProgress> progress;
        {
            std::unique_lock lock(mutex_);
            wake_.wait_until(lock,retryAt_,[this]{return stop_||retryNow_;});
            if(stop_)return;
            if(!retryNow_&&std::chrono::steady_clock::now()<retryAt_)continue;
            retryNow_=false;const int chosen=next();if(chosen<0)return;
            index=static_cast<size_t>(chosen);item=items_[index];items_[index].active=true;active_=chosen;
            current_=progress=std::make_shared<DownloadProgress>();preempted_=false;
        }
        try{
            const auto directory=destination();const auto partial=directory/(item.file+".part"),final=directory/item.file;
            std::error_code error;const std::uint64_t have=fs::exists(partial,error)?fs::file_size(partial,error):0;
            if(fs::space(directory).available<item.bytes-std::min(have,item.bytes)+options_.reserveBytes)
                throw std::runtime_error("Espaco em disco insuficiente para "+item.file);
            progress->stage(item.file,0);
            httpDownloadFile(options_.baseUrl+"/"+catalog_.release+"/"+item.file,partial,item.bytes,progress.get(),options_.stallSeconds);
            const auto& expected=catalog_.containers[index];
            if(fs::file_size(partial)!=expected.bytes)throw std::runtime_error("Download incompleto: "+item.file);
            if(fileSha256(partial,progress.get())!=expected.sha256){fs::remove(partial,error);throw std::runtime_error("SHA-256 divergente: "+item.file);}
            fs::remove(fs::path(final)+=".verified",error);fs::remove(final,error);fs::rename(partial,final);
            recordVerifiedContainer(final,expected);
            installVisualAssetContainer(final);
            std::lock_guard lock(mutex_);items_[index].done=true;items_[index].active=false;items_[index].received=item.bytes;items_[index].finished=finished_++;
            active_=-1;current_.reset();failures_=0;error_.clear();
        }catch(const std::exception& failure){
            std::lock_guard lock(mutex_);items_[index].active=false;active_=-1;current_.reset();
            if(stop_)return;
            if(preempted_){preempted_=false;retryNow_=true;continue;} // Yielded to the focused body.
            error_=failure.what();retryAt_=std::chrono::steady_clock::now()+options_.backoff[std::min(failures_,options_.backoff.size()-1)];++failures_;
        }
    }
}

albert::AssetDownloads::Status albert::AssetDownloads::status() const{
    std::lock_guard lock(mutex_);Status value;value.items=items_;value.error=error_;value.focus=focus_;
    if(active_>=0&&current_){const auto snapshot=current_->snapshot();value.source=snapshot.source;value.items[static_cast<size_t>(active_)].received=static_cast<std::uint64_t>(std::max<std::int64_t>(0,snapshot.received));}
    for(const auto& item:value.items){value.totalBytes+=item.bytes;value.doneBytes+=item.done?item.bytes:std::min(item.received,item.bytes);}
    value.essentialReady=!items_.empty()&&items_.front().done;
    value.complete=std::all_of(items_.begin(),items_.end(),[](const Item& i){return i.done;});
    if(!error_.empty())value.retrySeconds=static_cast<int>(std::max<std::int64_t>(0,std::chrono::duration_cast<std::chrono::seconds>(retryAt_-std::chrono::steady_clock::now()).count()));
    return value;
}
