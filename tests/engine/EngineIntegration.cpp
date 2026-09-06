#include "engine/ArchiveEngine.h"
#include "platform/MacMetadata.h"
#include <sys/xattr.h>
#include <sys/stat.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <unistd.h>

namespace fs=std::filesystem;
void check(bool condition,const std::string &message){if(!condition)throw std::runtime_error(message);}
std::string read(const fs::path &p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
void good(const m7arc::OperationResult &r,const std::string &label){if(!r.ok()){std::string errors;for(const auto &e:r.errors)errors+="\n"+e;throw std::runtime_error(label+errors);}}
int main(){
    fs::path root=fs::temp_directory_path()/("m7arc-engine-test-"+std::to_string(getpid()));
    try{
        fs::create_directories(root/"input/empty");std::ofstream(root/"input/中文.txt")<<"hello archive\n";std::ofstream(root/"input/script.sh")<<"#!/bin/sh\necho hi\n";fs::permissions(root/"input/script.sh",fs::perms::owner_all);
        m7arc::ArchiveEngine engine;std::cerr<<"Registered "<<engine.formats().size()<<" formats, "<<engine.codecs().size()<<" codecs, "<<engine.hashers().size()<<" hashers\n";for(const auto&f:engine.formats())std::cerr<<f.name<<" ";std::cerr<<"\n";check(engine.formats().size()==61,"All 61 registered formats must link");check(engine.codecs().size()==25,"All 25 codecs must link");check(engine.hashers().size()==10,"All 10 hashers must link");
        for(const std::string format:{"7z","zip","tar","wim","gzip","bzip2","xz","tar.gz","tar.bz2","tar.xz"}){
            m7arc::CreateOptions create;create.format=format;create.threads=2;
            const bool stream=format=="gzip"||format=="bzip2"||format=="xz";
            auto path=root/("test."+format);good(engine.create({(root/(stream?"input/中文.txt":"input")).string()},path.string(),create),"Create "+format);
            auto list=engine.list(path.string());check(!list.entries.empty(),"List "+format);good(engine.test(path.string()),"Test "+format);
            m7arc::ExtractOptions extract;extract.overwrite=m7arc::Overwrite::Replace;auto dest=root/("out-"+format);good(engine.extract(path.string(),dest.string(),{},extract),"Extract "+format);
            bool content=false;for(const auto &e:fs::recursive_directory_iterator(dest))if(e.is_regular_file()&&read(e.path())=="hello archive\n")content=true;
            check(content,"Byte roundtrip "+format);
        }
        auto hashes=engine.hash({(root/"input/中文.txt").string()},{"SHA256","CRC32"});check(hashes.size()==2,"Two checksum results");
        m7arc::CreateOptions encrypted;encrypted.password="test password";encrypted.encryptHeaders=true;auto enc=root/"secret.7z";good(engine.create({(root/"input").string()},enc.string(),encrypted),"Encrypted create");
        m7arc::OpenOptions password;password.password=encrypted.password;good(engine.test(enc.string(),{},password),"Encrypted test");
        m7arc::Callbacks ask;ask.password=[](){return std::optional<std::string>("test password");};check(!engine.list(enc.string(),{},ask).entries.empty(),"Password callback");
        auto list=engine.list((root/"test.7z").string());std::uint32_t member=0;for(const auto &e:list.entries)if(e.path.ends_with("中文.txt"))member=e.index;
        m7arc::ExtractOptions extract;extract.overwrite=m7arc::Overwrite::Replace;auto selected=root/"selected";good(engine.extract((root/"test.7z").string(),selected.string(),{member},extract),"Selected extract");int selectedFiles=0;for(const auto &e:fs::recursive_directory_iterator(selected))if(e.is_regular_file())++selectedFiles;check(selectedFiles==1,"Selection extracts exactly one member");
        good(engine.rename((root/"test.7z").string(),member,"input/renamed.txt"),"Rename");list=engine.list((root/"test.7z").string());bool renamed=false;for(const auto &e:list.entries)if(e.path=="input/renamed.txt"){renamed=true;member=e.index;}check(renamed,"Rename visible");
        good(engine.remove((root/"test.7z").string(),{member}),"Delete");list=engine.list((root/"test.7z").string());for(const auto &e:list.entries)check(e.path!="input/renamed.txt","Deleted member absent");good(engine.test((root/"test.7z").string()),"Archive valid after updates");
        m7arc::CreateOptions split;split.volumeSizes={128};auto splitPath=root/"split.7z";auto splitResult=engine.create({(root/"input").string()},splitPath.string(),split);good(splitResult,"Split create");check(splitResult.outputPaths.size()>1,"Multiple split volumes");good(engine.test(splitPath.string()+".001"),"Split test");
        std::ofstream(root/"abc.txt")<<"abc";
        auto knownHash=engine.hash({(root/"abc.txt").string()},{"SHA256"});check(knownHash[0].digest=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA256 known vector");
        std::ofstream(root/"manifest.sha256")<<knownHash[0].digest<<"  abc.txt\n";
        good(engine.test((root/"manifest.sha256").string()),"Verify checksum manifest");
        std::ofstream(root/"abc.txt")<<"changed";check(!engine.test((root/"manifest.sha256").string()).ok(),"Checksum verification detects mismatch");
        m7arc::CreateOptions advanced;advanced.method="LZMA2";advanced.dictionaryBytes=1<<20;advanced.wordSize=64;advanced.solidBlock="16m";advanced.threads=2;
        good(engine.create({(root/"input").string()},(root/"advanced.7z").string(),advanced),"Advanced 7z properties");
        advanced.format="zip";advanced.method="Deflate";advanced.dictionaryBytes=0;advanced.wordSize=32;advanced.password="zip password";advanced.encryptionMethod="AES256";
        good(engine.create({(root/"input").string()},(root/"aes.zip").string(),advanced),"ZIP AES create");good(engine.test((root/"aes.zip").string(),{},{advanced.password,{}}),"ZIP AES test");
        auto existing=root/"preserve.7z";good(engine.create({(root/"input").string()},existing.string()),"Create original for cancellation");auto originalBytes=read(existing);
        m7arc::Callbacks cancel;cancel.progress=[](const m7arc::Progress&p){return p.phase!="Compressing";};auto cancelled=engine.create({(root/"abc.txt").string()},existing.string(),{},cancel);check(cancelled.cancelled,"Cancel update callback");check(read(existing)==originalBytes,"Cancelled update leaves original bytes unchanged");
        auto conflict=root/"conflict";good(engine.extract((root/"test.zip").string(),conflict.string(),{},extract),"Prepare overwrite destination");std::ofstream(conflict/"input/中文.txt")<<"keep original";
        m7arc::Callbacks skip;int asked=0;skip.overwrite=[&](const m7arc::OverwriteRequest&){++asked;return m7arc::OverwriteAnswer::Skip;};m7arc::ExtractOptions askExtract;good(engine.extract((root/"test.zip").string(),conflict.string(),{},askExtract,skip),"Skip overwrite");check(asked>0&&read(conflict/"input/中文.txt")=="keep original","Overwrite callback preserves skipped file");
        const auto nativeFile=root/"input/中文.txt";const std::string attribute="finder metadata";check(setxattr(nativeFile.c_str(),"com.m7arc.test",attribute.data(),attribute.size(),0,0)==0,"Set source xattr");
        const m7arc::mac::FileTime birth{1700000000,123456789};m7arc::mac::setCreationTime(nativeFile,birth);
        for(const std::string f:{"7z","zip","tar","wim","tar.gz"}){
            m7arc::CreateOptions settings;settings.format=f;auto a=root/("metadata."+f);good(engine.create({(root/"input").string()},a.string(),settings),"Metadata archive "+f);
            const auto d=root/("metadata-out-"+f);good(engine.extract(a.string(),d.string(),{},extract),"Metadata extract "+f);char value[128]{};const auto target=d/"input/中文.txt";
            const auto length=getxattr(target.c_str(),"com.m7arc.test",value,sizeof(value),0,0);check(length==static_cast<ssize_t>(attribute.size())&&std::string(value,length)==attribute,"Native xattr roundtrip "+f);check(m7arc::mac::creationTime(target)==birth,"Birth time roundtrip "+f);
        }
        auto metadataArchive=root/"metadata.7z";auto metadataList=engine.list(metadataArchive.string());std::uint32_t metadataMember=0;for(const auto &e:metadataList.entries)if(e.path=="input/中文.txt")metadataMember=e.index;
        const auto skipped=root/"metadata-skip/input/中文.txt";fs::create_directories(skipped.parent_path());std::ofstream(skipped)<<"existing";check(setxattr(skipped.c_str(),"com.m7arc.test","untouched",9,0,0)==0,"Set existing metadata");m7arc::ExtractOptions skipExisting;skipExisting.overwrite=m7arc::Overwrite::Skip;
        good(engine.extract(metadataArchive.string(),(root/"metadata-skip").string(),{metadataMember},skipExisting),"Metadata skip");char oldAttr[128]{};check(getxattr(skipped.c_str(),"com.m7arc.test",oldAttr,sizeof(oldAttr),0,0)==9&&std::string(oldAttr,9)=="untouched","Skipping file preserves original metadata");
        m7arc::ExtractOptions renameIncoming;renameIncoming.overwrite=m7arc::Overwrite::RenameIncoming;good(engine.extract(metadataArchive.string(),(root/"metadata-skip").string(),{metadataMember},renameIncoming),"Metadata auto rename");bool renamedMetadata=false;for(const auto&e:fs::directory_iterator(skipped.parent_path()))if(e.path()!=skipped){char v[128]{};const auto n=getxattr(e.path().c_str(),"com.m7arc.test",v,sizeof(v),0,0);if(n==static_cast<ssize_t>(attribute.size())&&std::string(v,n)==attribute)renamedMetadata=true;}check(renamedMetadata,"Auto-renamed file receives matching metadata");
        good(engine.rename(metadataArchive.string(),metadataMember,"input/native-renamed.txt"),"Rename metadata member");good(engine.extract(metadataArchive.string(),(root/"metadata-renamed").string(),{},extract),"Extract renamed metadata");char renamedAttr[128]{};check(getxattr((root/"metadata-renamed/input/native-renamed.txt").c_str(),"com.m7arc.test",renamedAttr,sizeof(renamedAttr),0,0)==static_cast<ssize_t>(attribute.size()),"Rename carries sidecar");
        m7arc::BenchmarkOptions bench;bench.threads=1;bench.dictionaryBytes=1<<18;bench.seconds=1;auto rates=engine.benchmark(bench);check(rates.encodeBytesPerSecond>0&&rates.decodeBytesPerSecond>0,"Official benchmark reports actual throughput");
        std::cout<<"All formats, archive operations, cancellation, overwrite, checksum verification and benchmark integration passed\n";fs::remove_all(root);return 0;
    }catch(const std::exception&e){std::cerr<<e.what()<<"\nFixtures: "<<root<<'\n';return 1;}
}
