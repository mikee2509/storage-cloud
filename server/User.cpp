#include "User.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fstream>
#include <functional>

using namespace mongocxx;
using std::map;
using std::vector;

using bsoncxx::builder::basic::make_array;

User::User(oid& id1, UserManager& u_m): id(id1), user_manager(u_m), authorized(false), valid(true), currentInFileValid(false) {}

User::User(UserManager& u_m):user_manager(u_m), authorized(false), valid(false), currentInFileValid(false) {}

User::User(const string& username, UserManager& u_m): user_manager(u_m), authorized(false), valid(false), currentInFileValid(false) {
    addUsername(username);
}

bool User::addUsername(const string &username) {
    oid tmp_id;

    if(!user_manager.getUserId(username, tmp_id)) {
        valid = false;
        authorized = false;
        return false;
    }

    if(valid) {
        if(tmp_id != id) {
            id = tmp_id;
            authorized = false;
        }
        return true;
    } else {
        id = tmp_id;
        authorized = false;
        valid = true;
    }

    return true;
}

bool User::getName(string& name) {
    return user_manager.getName(id, name);
}

bool User::getSurname(string& name) {
    return user_manager.getSurname(id, name);
}

bool User::getHomeDir(string& dir) {
    return user_manager.getHomeDir(id, dir);
}

bool User::setName(string& name) {
    return user_manager.setName(id, name);
}

bool User::setPassword(const string& passwd) {
    return user_manager.setPasswd(id, passwd);
}

bool User::checkPassword(const string& passwd) {
    return user_manager.checkPasswd(id, passwd);
}

bool User::loginByPassword(string& password, string& sid) {
    authorized = false;
    if(checkPassword(password)) {
        uint8_t t_sid_b[48];
        if(RAND_bytes(t_sid_b, 48) != 1) {
            return false;
        }

        string t_sid((char*)t_sid_b, 48);

        if(!user_manager.addSid(id, t_sid)) {
            return false;
        }

        sid = t_sid;
        authorized = true;
        return true;
    }

    return false;
}

bool User::loginBySid(string& sid) {
    authorized = user_manager.checkSid(id, sid);
    return authorized;
}

bool User::logout(string& sid) {
    valid = authorized = false;
    return user_manager.removeSid(id, sid);
}


bool User::listFilesinPath(const string& path, vector<UFile>& res) {
    return user_manager.listFilesinPath(id, path, res);
}

// also adds directory
uint8_t User::addFile(UFile& file) {
    currentInFileValid = false;
    if(file.filename[0] != '/') {
        return ADD_FILE_WRONG_DIR;
    }

    uint64_t pos;

    if((pos = file.filename.rfind('/')) == string::npos) {
        return ADD_FILE_WRONG_DIR;
    }

    if(pos == file.filename.size() - 1) {
        return ADD_FILE_EMPTY_NAME;
    }

    string dir = file.filename.substr(0, pos);

    if(!dir.empty() && !user_manager.yourFileIsDir(id, dir)) {
        return ADD_FILE_WRONG_DIR;
    }

    if(user_manager.yourFileExists(id, file.filename)) {
        if(file.type == FILE_REGULAR) {
            UFile tmp_file;
            if(user_manager.getYourFileMetadata(id, file.filename, tmp_file, FILE_REGULAR)) {
                if(!tmp_file.isValid && tmp_file.size == file.size && tmp_file.hash == file.hash) {
                    uint64_t spaceNeeded = tmp_file.size - tmp_file.lastValid;
                    uint64_t availableSpace;
                    if(!user_manager.getFreeSpace(id, availableSpace) || availableSpace < spaceNeeded) {
                        return ADD_FILE_NO_SPACE;
                    }
                    currentInFile = tmp_file;
                    currentInFile.owner = id;
                    currentInFileValid = true;
                    return ADD_FILE_CONTINUE_OK;
                }
            }
        }

        return ADD_FILE_FILE_EXISTS;
    }

    oid fileId;

    if(user_manager.addNewFile(id, file, dir, fileId)) {
        if(file.type == FILE_REGULAR) {
            uint64_t availableSpace;
            if(!user_manager.getFreeSpace(id, availableSpace) || availableSpace < file.size) {
                return ADD_FILE_NO_SPACE;
            }

            currentInFile = file;
            currentInFile.isValid = false;
            currentInFile.lastValid = 0;
            currentInFile.id = fileId;
            currentInFile.owner = id;
            currentInFileValid = true;
        }

        return ADD_FILE_OK;
    }

    return ADD_FILE_INTERNAL_ERROR;
}

const UFile& User::getCurrentInFileMetadata() {
    return currentInFile;
}

bool User::isCurrentInFileValid() {
    return currentInFileValid;
}

bool User::addFileChunk(const string& chunk) {
    if(!currentInFileValid) {
        return false;
    }

    uint64_t freeSpace;
    if(!user_manager.getFreeSpace(id, freeSpace) || freeSpace < chunk.size()) {
        return false;
    }

    if(currentInFile.size < currentInFile.lastValid + chunk.size()) {
        return false;
    }

    if(!user_manager.addFileChunk(currentInFile, chunk)) {
        return false;
    }

    if(currentInFile.size != currentInFile.lastValid) {
        return true;
    }

    return user_manager.validateFile(currentInFile);
}

bool User::isAdmin() {
    if(valid && authorized) {
        uint64_t role;
        if(user_manager.getUserRole(id, role) && role == USER_ADMIN) {
            return true;
        }
    }

    return false;
}

bool User::listUserFiles(string& username, string& path, vector<UFile>& files) { ;
    return user_manager.runAsUser(username, [&path, &files, this](oid& id) -> bool {return user_manager.listFilesinPath(id, path, files);});
}

bool User::getYourStats(UDetails& stats) {
    return user_manager.getUserDetails(id, stats);
}

bool User::deleteFile(const string& path) {
    if(!user_manager.yourFileExists(id, path)) {
        return false;
    }

    if(user_manager.yourFileIsDir(id, path)) {
        return user_manager.deletePath(id, path);
    }

    return user_manager.deleteFile(id, path);
}

bool User::deleteUserFile(const string& username, const string& path) {

    if(!user_manager.runAsUser(username, [&path, this](oid& id) -> bool {return user_manager.yourFileExists(id, path);})) {
        return false;
    }

    if(user_manager.runAsUser(username, [&path, this](oid& id) -> bool
        {return user_manager.yourFileIsDir(id, path);})
    ) {
        return user_manager.runAsUser(username, [&path, this](oid& id) -> bool {return user_manager.deletePath(id, path);});
    }

    return user_manager.runAsUser(username, [&path, this](oid& id) -> bool {return user_manager.deleteFile(id, path);});
}

bool User::changePasswd(const string& old, const string& new_passwd) {
    if(checkPassword(old)) {
        return setPassword(new_passwd);
    }

    return false;
}

bool User::changeUserPasswd(const string& username, const string& new_passwd) {
    return user_manager.runAsUser(username, [&new_passwd, this](oid& id) -> bool {return user_manager.setPasswd(id, new_passwd);});
}

bool User::initFileDownload(const string& filename, const uint64_t pos, string& chunk) {
    currentOutFileValid = false;
    if(!user_manager.yourFileExists(id, filename)) {
        return false;
    }

    if(!user_manager.getYourFileMetadata(id, filename, currentOutFile, FILE_REGULAR)) {
        return false;
    }

    if(pos >= currentOutFile.size) {
        return false;
    }

    currentOutFileValid = true;
    currentOutFile.lastValid = pos;

    return getFileChunk(chunk);
}

bool User::initSharedFileDownload(const string& filename, const string& ownerUsername, const string& hash, const uint64_t pos, string& chunk) {
    oid ownerId, fileId;
    if(!user_manager.getUserId(ownerUsername, ownerId)) {
        return false;
    }

    if(!user_manager.getFileIdAdvanced(ownerId, filename, hash, fileId)) {
        return false;
    }

    string realFilename;

    if(!user_manager.getFileFilename(fileId, realFilename)) {
        return false;
    }

    UFile file;

    if(!user_manager.getYourFileMetadata(ownerId, realFilename, file, FILE_REGULAR)) {
        return false;
    }

    if(pos >= currentOutFile.size) {
        return false;
    }

    currentOutFileValid = true;
    currentOutFile.lastValid = pos;

    return getFileChunk(chunk);
}

bool User::getFileChunk(string& chunk) {
    if(!currentOutFileValid) {
        return false;
    }

    if(!user_manager.getFileChunk(currentOutFile, chunk)) {
        return false;
    }

    if(currentOutFile.lastValid == currentOutFile.size) {
        currentOutFileValid = false;
    }

    return true;
}

bool User::shareWith(const string& filename, const string& username) {
    oid userId, fileId;
    if(user_manager.getUserId(username, userId) && user_manager.getFileId(id, filename, fileId)) {
        return user_manager.shareWith(fileId, userId);
    }

    return false;
}

bool User::unshareWith(const string& filename, const string& username) {
    oid userId, fileId;
    if(user_manager.getUserId(username, userId) && user_manager.getFileId(id, filename, fileId)) {
        return user_manager.unshareWith(fileId, userId);
    }

    return false;
}

bool User::listShared(vector<UFile>& list) {
    return user_manager.listSharedWithUser(id, list);
}

bool User::listUserShared(const string& username, vector<UFile>& list) {
    return user_manager.runAsUser(username, [&list, this](oid& id) -> bool {return user_manager.listSharedWithUser(id, list);});
}

bool User::clearCache() {
    return user_manager.removeAllUnfinishedForUser(id);
}

bool User::changeUserTotalStorage(const string& username, uint64_t newVal) {
    oid userId;
    if(!user_manager.getUserId(username, userId)) {
        return false;
    }

    uint64_t freeSpace, totalSpace;

    if(!user_manager.getFreeSpace(userId, freeSpace) || user_manager.getTotalSpace(userId, totalSpace)) {
        return false;
    }

    if(totalSpace - freeSpace > newVal) {
        return false;
    }

    return user_manager.setTotalSpace(userId, newVal);
}

bool User::shareInfo(const string& filename, vector<string>& res) {
    oid fileId;
    if(!user_manager.getFileId(id, filename, fileId)) {
        return false;
    }
    return user_manager.shareInfo(fileId, res);
}

bool User::shareInfoUser(const string& username, const string& filename, vector<string>& res) {
    oid fileId;
    if(!user_manager.runAsUser(username , [&filename, &fileId, this](oid& id) -> bool {return user_manager.getFileId(id, filename, fileId);})) {
        return false;
    }

    return user_manager.shareInfo(fileId, res);
}

bool User::getWarnings(vector<string>& list) {
    return user_manager.getWarningList(id, list);
}

bool User::warnUser(const string& username, const string& body) {
    return user_manager.runAsUser(username , [&body, this](oid& id) -> bool {return user_manager.addWarning(id, body);});
}

///---------------------UserManager---------------------

UserManager::UserManager(Database& db_t, Logger& logger_t): db(db_t), logger(logger_t) {}

std::thread UserManager::startGarbageCollector(std::condition_variable& g_cond, bool& should_exit) {
    return std::thread(&UserManager::garbageCollectorMain, this, std::ref(g_cond), std::ref(should_exit));
}

void UserManager::garbageCollectorMain(std::condition_variable& g_cond, bool& should_exit) {
    std::mutex g_mutex;
    while (!should_exit) {
        logger.log("UserManager", "running garbage collector");
        collectOldUnfinished();
        std::unique_lock<std::mutex> lock(g_mutex);
        g_cond.wait_for(lock, std::chrono::minutes(5));
    }
}

bool UserManager::getName(oid& id, string& res) {
    return db.getField("users", "name", id, res);
}

bool UserManager::getSurname(oid& id, string& res) {
    return db.getField("users", "surname", id, res);
}

bool UserManager::getHomeDir(oid& id, string& res) {
    return db.getField("users", "homeDir", id, res);
}

bool UserManager::setName(oid& id, string& res) {
    return db.setField("users", "name", id, res);
}

bool UserManager::getUserRole(oid& id, uint64_t& role) {
    return db.getField("users", "role", id, (int64_t&)role);
}

bool UserManager::getUserId(const string& username, oid& id) {
    return db.getId("users", "username", username, id);
}

bool UserManager::getPasswdHash(oid& id, string& res) {
    const uint8_t* tmp_b;
    uint32_t tmp_s;
    if(db.getField("users", "password", id, tmp_b, tmp_s)) {
        res = string((const char*) tmp_b, tmp_s);
        return true;
    }

    return false;
}

bool UserManager::checkPasswd(oid& id, const string& passwd) {
    auto digest = new uint8_t[SHA512_DIGEST_LENGTH];

    SHA512((const uint8_t*) passwd.c_str(), passwd.size(), digest);

    string hash_to_check((char*)digest, SHA512_DIGEST_LENGTH);
    string current_hash;

    if(!getPasswdHash(id, current_hash)) {
        delete digest;
        return false;
    }

    bool wyn = (current_hash == hash_to_check);
    delete digest;

    return wyn;
}

bool UserManager::setPasswd(oid& id, const string& passwd) {
    auto digest = new uint8_t[SHA512_DIGEST_LENGTH];

    SHA512((const uint8_t*) passwd.c_str(), passwd.size(), digest);

    string hash((char*)digest, SHA512_DIGEST_LENGTH);

    return db.setField("users", "password", id, (const uint8_t*) hash.c_str(), (uint32_t) hash.size());
}

bool UserManager::addSid(oid& id, string& sid) {
    return db.pushValToArr("users", "sids", id, make_document(
            kvp("sid", Database::stringToBinary(sid)),
            kvp("time", bsoncxx::types::b_date(std::chrono::system_clock::now()))
    ));
}

bool UserManager::checkSid(oid& id, string& sid) {
    uint64_t tmp_l = 0;
    return (db.countField("users", "sids.sid", id, (const uint8_t*) sid.c_str(), sid.size(), tmp_l) && tmp_l == 1);
}

bool UserManager::removeSid(oid& id, string &sid) {
    return db.removeFieldFromArray("users", "sids", id, make_document(kvp("sid", Database::stringToBinary(sid))));
}

bool UserManager::parseUserDetails(map<string, bsoncxx::types::value>& usr, UDetails& tmp_u) {
    try {
        tmp_u.username = bsoncxx::string::to_string(usr.find("username")->second.get_utf8().value);
        tmp_u.name = bsoncxx::string::to_string(usr.find("name")->second.get_utf8().value);
        tmp_u.surname = bsoncxx::string::to_string(usr.find("surname")->second.get_utf8().value);
        tmp_u.role = (uint8_t) usr.find("role")->second.get_int64().value;
        tmp_u.totalSpace = (uint64_t) usr.find("totalSpace")->second.get_int64().value;
        uint64_t freeSpace = (uint64_t) usr.find("freeSpace")->second.get_int64().value;
        tmp_u.usedSpace = tmp_u.totalSpace - freeSpace;
    } catch (const std::exception& ex) {
        logger.err(l_id, "error while parsing user details: " + string(ex.what()));
        return false;
    } catch (...) {
        logger.err(l_id, "error while parsing user details: unknown error");
        return false;
    }
    return true;
}

bool UserManager::getUserDetails(oid id, UDetails& userDetails) {
    map<string, bsoncxx::types::value> mmap;
    vector<string> fields{"username", "surname", "name", "role", "totalSpace", "freeSpace"};
    for(string field: fields) {
        mmap.emplace(field, bsoncxx::types::value(bsoncxx::types::b_null{}));
    }

    if(!db.getFields("users", id, mmap)) {
        return false;
    }

    return parseUserDetails(mmap, userDetails);
}

bool UserManager::registerUser(UDetails& user, const string& password, bool& userTaken) {
    uint64_t userCount = 1;
    userTaken = false;
    if(!(db.countField("users", "username", user.username, userCount) && userCount == 0)) {
        userTaken = true;
        return false;
    }

    auto doc = bsoncxx::builder::basic::document{};
    doc.append(kvp("username", toUTF8(user.username)));
    doc.append(kvp("name", toUTF8(user.name)));
    doc.append(kvp("surname", toUTF8(user.surname)));
    doc.append(kvp("role", toINT64(user.role)));

    if(user.role != USER_ADMIN) {
        doc.append(kvp("totalSpace", toINT64(defaultStorage)));
        doc.append(kvp("freeSpace", toINT64(defaultStorage)));

        string homeDir = "/" + user.username;

        doc.append(kvp("homeDir", toUTF8(homeDir)));

        string fullPath = root_path + homeDir;

        int mkdir_res = mkdir(fullPath.c_str(), S_IRWXU);

        if(mkdir_res != 0) {
            logger.err(l_id, "error while trying to mkdir " + fullPath + " for new user");
            return false;
        }
    } else {
        doc.append(kvp("totalSpace", toINT64(0)));
        doc.append(kvp("freeSpace", toINT64(0)));
        doc.append(kvp("homeDir", ""));
    }

    oid tmp_id;

    if(!db.insertDoc("users", tmp_id, doc)) {
        return false;
    }

    return setPasswd(tmp_id, password);
}

bool UserManager::listAllUsers(std::vector<UDetails>& res) {
    map<bsoncxx::oid, map<string, bsoncxx::types::value> > mmap;
    vector<string> fields{"username", "surname", "name", "role", "totalSpace", "freeSpace"};

    if(!db.getFields("users", fields, mmap)) {
        return false;
    }

    for(auto &usr: mmap) {
        UDetails tmp_u;
        if(parseUserDetails(usr.second, tmp_u)) {
            res.emplace_back(tmp_u);
        } else {
            return false;
        }
    }

    return true;
}

bool UserManager::listFilesinPath(oid& id, const string& path, vector<UFile>& files) {
    map<string, map<string, bsoncxx::types::value> > mmap;
    vector<string> fields;
    fields.emplace_back("filename");
    fields.emplace_back("size");
    fields.emplace_back("creationDate");
    fields.emplace_back("ownerName");
    fields.emplace_back("type");
    fields.emplace_back("hash");
    fields.emplace_back("isShared");

    string parsedPath = path;

    if(parsedPath[parsedPath.size()-1] != '/') {
        parsedPath.push_back('/');
    }

    mongocxx::pipeline stages;

    stages.match(make_document(kvp("owner", id), kvp("isValid", true), kvp("filename", bsoncxx::types::b_regex("^"+parsedPath+"[^/]*[^/]$"))));
    stages.lookup(make_document(kvp("from", "users"), kvp("localField", "owner"), kvp("foreignField", "_id"), kvp("as", "ownerTMP")));
    stages.unwind("$ownerTMP");
    stages.add_fields(make_document(kvp("ownerName", make_document(kvp("$concat", make_array("$ownerTMP.name", " ", "$ownerTMP.surname"))))));
    stages.add_fields(make_document(kvp("isShared", make_document(kvp("$and", make_array(
            make_document(kvp("$eq", make_array(make_document(kvp("$type", "$sharedWith")), "array"))),
            make_document(kvp("$gt", make_array(make_document(kvp("$size", "$sharedWith")), 0)))
    ))))));
    stages.project(make_document(kvp("ownerTMP", 0), kvp("_id", 0)));

    if(!db.getFieldsAdvanced("files", stages, fields, mmap)) {
        return false;
    }

    try {
        for(std::pair<string, map<string, bsoncxx::types::value> > usr: mmap) {

            UFile tmp;
            tmp.filename = usr.first;
            tmp.size = (uint64_t) usr.second.find("size")->second.get_int64();
            tmp.creation_date = (uint64_t) usr.second.find("creationDate")->second.get_date().to_int64();
            tmp.owner_name = bsoncxx::string::to_string(usr.second.find("ownerName")->second.get_utf8().value);
            tmp.type = (uint8_t) usr.second.find("type")->second.get_int64().value;
            tmp.hash = string((const char*) usr.second.find("hash")->second.get_binary().bytes, usr.second.find("hash")->second.get_binary().size);
            tmp.isShared = usr.second.find("isShared")->second.get_bool();

            files.emplace_back(tmp);
        }
    } catch (const std::exception& ex) {
        logger.err(l_id, "error while parsing file list: " + string(ex.what()));
        return false;
    } catch (...) {
        logger.err(l_id, "error while parsing file list: unknown error");
        return false;
    }

    return true;
}

bsoncxx::types::b_utf8 UserManager::toUTF8(string& s) {
    return bsoncxx::types::b_utf8(s);
}

bsoncxx::types::b_int64 UserManager::toINT64(uint64_t i) {
    bsoncxx::types::b_int64 tmp{};
    tmp.value = i;
    return tmp;
}

bsoncxx::types::b_date UserManager::currDate() {
    return bsoncxx::types::b_date(std::chrono::system_clock::now());
}

bsoncxx::types::b_oid UserManager::toOID(oid i) {
    bsoncxx::types::b_oid tmp{};
    tmp.value = i;
    return tmp;
}

bsoncxx::types::b_bool UserManager::toBool(bool b) {
    bsoncxx::types::b_bool tmp{};
    tmp.value = b;
    return tmp;
}

bsoncxx::types::b_binary UserManager::toBinary(string& str) {
    bsoncxx::types::b_binary b_sid{};
    b_sid.bytes = (const uint8_t*) str.c_str();
    b_sid.size = (uint32_t) str.size();
    return b_sid;
}

// also adds directory
bool UserManager::addNewFile(oid& id, UFile& file, string& dir, oid& newId) {
    auto doc = bsoncxx::builder::basic::document{};

    if(file.type == FILE_REGULAR) {
        doc.append(kvp("filename", toUTF8(file.filename)));
        doc.append(kvp("size", toINT64(file.size)));
        doc.append(kvp("creationDate", currDate()));
        doc.append(kvp("type", toINT64(FILE_REGULAR)));
        doc.append(kvp("hash", toBinary(file.hash)));
        doc.append(kvp("isValid", toBool(false)));
        doc.append(kvp("lastValid", toINT64(0)));
        doc.append(kvp("owner", toOID(id)));

        string homeDir;

        if(!getHomeDir(id, homeDir)) {
            return false;
        }

        string fullPath = root_path + homeDir + file.filename;

        std::fstream fs;
        fs.open(fullPath, std::ios::out);

        if(!fs.is_open()) {
            return false;
        }

        fs.close();

        if(!db.insertDoc("files", newId, doc)) {
            remove(fullPath.c_str());
            return false;
        }

        file.realPath = fullPath;
    } else if(file.type == FILE_DIR) {
        string tmp = "";
        doc.append(kvp("filename", toUTF8(file.filename)));
        doc.append(kvp("size", toINT64(0)));
        doc.append(kvp("creationDate", currDate()));
        doc.append(kvp("type", toINT64(FILE_DIR)));
        doc.append(kvp("owner", toOID(id)));
        doc.append(kvp("hash", toBinary(tmp)));
        doc.append(kvp("isValid", toBool(true)));
        doc.append(kvp("lastValid", toINT64(0)));

        string homeDir;

        if(!getHomeDir(id, homeDir)) {
            return false;
        }

        string fullPath = root_path + homeDir + file.filename;
        int mkdir_res = mkdir(fullPath.c_str(), S_IRWXU);

        if(mkdir_res != 0) {
            logger.err(l_id, "mkdir error while trying to create directory: " + fullPath);
            return false;
        }

        if(!db.insertDoc("files", newId, doc)) {
            rmdir(fullPath.c_str());
            return false;
        }

        file.realPath = fullPath;
    } else {
        return false;
    }

    // if not root dir
    if(!dir.empty()) {
        db.incField("files", "size", "owner", id, "filename", dir);
    }

    return true;
}

bool UserManager::yourFileExists(oid &id, const string &filename) {
    uint64_t tmp_l = 0;
    return (db.countField("files", "filename", filename, "owner", id, tmp_l) && tmp_l == 1);
}

bool UserManager::yourFileIsDir(oid &id, const string &path) {
    int64_t type;
    if(!db.getField("files", "type", "owner", id, "filename", path, type)) {
        return false;
    }

    return type == FILE_DIR;
}

bool UserManager::getYourFileMetadata(oid& id, const string& filename, UFile& file, uint8_t type) {
    map<string, map<string, bsoncxx::types::value> > mmap;
    vector<string> fields;
    fields.emplace_back("filename");
    fields.emplace_back("size");
    fields.emplace_back("creationDate");
    fields.emplace_back("ownerName");
    fields.emplace_back("ownerUsername");
    fields.emplace_back("type");
    fields.emplace_back("hash");
    fields.emplace_back("isValid");
    fields.emplace_back("isShared");
    fields.emplace_back("_id");
    if(type == FILE_REGULAR) {
        fields.emplace_back("lastValid");
    }
    
    mongocxx::pipeline stages;

    stages.match(make_document(kvp("owner", id), kvp("filename", filename), kvp("type", type)));
    stages.lookup(make_document(kvp("from", "users"), kvp("localField", "owner"), kvp("foreignField", "_id"), kvp("as", "ownerTMP")));
    stages.unwind("$ownerTMP");
    stages.add_fields(make_document(kvp("ownerName", make_document(kvp("$concat", make_array("$ownerTMP.name", " ", "$ownerTMP.surname")))),
                                    kvp("ownerUsername", "$ownerTMP.username")));
    stages.add_fields(make_document(kvp("isShared", make_document(kvp("$and", make_array(
            make_document(kvp("$eq", make_array(make_document(kvp("$type", "$sharedWith")), "array"))),
            make_document(kvp("$gt", make_array(make_document(kvp("$size", "$sharedWith")), 0)))
    ))))));
    stages.project(make_document(kvp("ownerTMP", 0)));

    if(!db.getFieldsAdvanced("files", stages, fields, mmap)) {
        return false;
    }
    
    if(mmap.size() != 1) {
        return false;
    }

    try {
        auto tmp_file = *(mmap.begin());

        file.filename = bsoncxx::string::to_string(tmp_file.second.find("filename")->second.get_utf8().value);
        file.size = (uint64_t) tmp_file.second.find("size")->second.get_int64().value;
        file.creation_date = (uint64_t) tmp_file.second.find("creationDate")->second.get_date().to_int64();
        file.owner_name = bsoncxx::string::to_string(tmp_file.second.find("ownerName")->second.get_utf8().value);
        file.type = (uint8_t) tmp_file.second.find("type")->second.get_int64().value;
        file.hash = string((const char*) tmp_file.second.find("hash")->second.get_binary().bytes, tmp_file.second.find("hash")->second.get_binary().size);
        file.isValid = tmp_file.second.find("isValid")->second.get_bool();
        file.id = tmp_file.second.find("_id")->second.get_oid().value;
        file.isShared = tmp_file.second.find("isShared")->second.get_bool();
        file.owner = id;
        if(type == FILE_REGULAR) {
            file.lastValid = (uint64_t) tmp_file.second.find("lastValid")->second.get_int64();
        }

        string homeDir;

        if(!getHomeDir(id, homeDir)) {
            return false;
        }

        file.realPath = root_path + homeDir + file.filename;
    } catch (const std::exception& ex) {
        logger.err(l_id, "error while parsing file metadata: " + string(ex.what()));
        return false;
    } catch (...) {
        logger.err(l_id, "error while parsing file metadata: unknown error");
        return false;
    }

    return true;
}

bool UserManager::addFileChunk(UFile& file, const string& chunk) {
    std::fstream fs;
    if(file.lastValid == 0) {
        fs.open(file.realPath, std::ios::out | std::ios::binary | std::ios::trunc);
    } else {
        fs.open(file.realPath, std::ios::out | std::ios::binary | std::ios::in);
    }
    if(!fs.is_open()) {
        return false;
    }
    fs.seekp(file.lastValid, std::ios::beg);

    fs.write(chunk.c_str(), chunk.size());
    if(fs.bad()) {
        return false;
    }

    fs.close();

    file.lastValid += chunk.size();

    if(db.incField("files", file.id, "lastValid", chunk.size())) {
        return updateLastChunkTime(file) && changeFreeSpace(file.owner, -chunk.size());
    }

    return false;
}

bool UserManager::validateFile(UFile& file) {
    uint8_t hash[FILE_HASH_SIZE];
    SHA_CTX sha1;
    SHA1_Init(&sha1);
    const int bufSize = 32768;
    uint8_t* buffer = new uint8_t[bufSize];

    std::ifstream is (file.realPath, std::ios::binary | std::ios::in);
    if(!is.is_open()) {
        deleteFile(file.owner, file.filename);
        return false;
    }

    is.seekg (0, is.end);
    uint64_t length = is.tellg();
    is.seekg (0, is.beg);

    if(length != file.size) {
        deleteFile(file.owner, file.filename);
        return false;
    }

    do {
        is.read((char*) buffer, bufSize);
        if(is.bad()) {
            break;
        }
        SHA1_Update(&sha1, buffer, is.gcount());
    } while(!is.eof());

    delete buffer;

    if(!is.eof()) {
        deleteFile(file.owner, file.filename);
        return false;
    }

    SHA1_Final(hash, &sha1);

    is.close();

    for(int i=0; i<FILE_HASH_SIZE; i++) {
        if((uint8_t) file.hash[i] != hash[i]) {
            deleteFile(file.owner, file.filename);
            return false;
        }
    }

    if(!db.setField("files", "isValid", file.id, true)) {
        deleteFile(file.owner, file.filename);
        return false;
    }

    file.isValid = true;

    return true;
}

bool UserManager::runAsUser(const string& username, std::function<bool(oid&)> fun) {
    oid tmp_id;
    if(!getUserId(username, tmp_id)) {
        return false;
    }

    return fun(tmp_id);
}

static int rmFiles(const char *pathname, const struct stat *sbuf, int type, struct FTW *ftwb)
{
    if(remove(pathname) < 0)
    {
        perror("ERROR: remove");
        return -1;
    }
    return 0;
}

bool UserManager::deleteUser(const string& username) {
    oid id;
    if(!getUserId(username, id)) {
        return false;
    }

    string home_dir;

    if(!getHomeDir(id, home_dir)) {
        return false;
    }

    string realPath = root_path + home_dir;

    if (nftw(realPath.c_str(), rmFiles, 10, FTW_DEPTH|FTW_MOUNT|FTW_PHYS) < 0)
    {
        return false;
    }

    db.removeByOid("files", "owner", id);
    db.removeByOid("users", "_id", id);

    bsoncxx::types::b_oid id_obj;
    id_obj.value = id;
    db.removeFieldFromArrays("files", "sharedWith", "userId", bsoncxx::types::value{id_obj});

    return true;
}

bool UserManager::deleteFile(oid& id, const string& path) {
    UFile details;
    if(!getYourFileMetadata(id, path, details, FILE_REGULAR)) {
        return false;
    }

    string home_dir;

    if(!getHomeDir(id, home_dir)) {
        return false;
    }

    string realPath = root_path + home_dir + path;

    remove(realPath.c_str());

    db.removeByOid("files", "_id", details.id);

    changeFreeSpace(id, details.lastValid);

    return true;
}

bool UserManager::deletePath(oid& id, const string& path) {
    map<string, map<string, bsoncxx::types::value> > mmap;
    vector<string> fields;
    fields.emplace_back("totalSize");

    string parsedPath = path;

    if(parsedPath[parsedPath.size()-1] != '/') {
        parsedPath.push_back('/');
    }

    mongocxx::pipeline stages;

    stages.match(make_document(kvp("owner", id), kvp("type", FILE_REGULAR), kvp("filename", bsoncxx::types::b_regex("^"+parsedPath))));
    stages.group(make_document(kvp("_id", bsoncxx::types::b_null{}), kvp("totalSize", make_document(kvp("$sum", "$lastValid")))));
    stages.project(make_document(kvp("_id", 0)));

    uint64_t totalSize = 0;

    if(!db.sumFieldAdvanced("files", "totalSize", stages, totalSize)) {
        return false;
    }

    string home_dir;

    if(!getHomeDir(id, home_dir)) {
        return false;
    }

    parsedPath.pop_back();
    string realPath = root_path + home_dir + parsedPath;

    if (nftw(realPath.c_str(), rmFiles, 10, FTW_DEPTH | FTW_MOUNT | FTW_PHYS) < 0) {
        return false;
    }

    db.deleteDocs("files", make_document(kvp("owner", id), kvp("filename", bsoncxx::types::b_regex("^"+parsedPath+"($|(/.+))"))));

    string dir = parsedPath.substr(0, parsedPath.rfind('/'));

    if(!dir.empty()) {
        db.incField("files", "size", "owner", id, "filename", dir, -1);
    }

    changeFreeSpace(id, totalSize);

    return true;
}

bool UserManager::getFileChunk(UFile& file, string& chunk) {
    uint64_t toRead = (file.size - file.lastValid > OUT_FILE_CHUNK_SIZE) ? OUT_FILE_CHUNK_SIZE : (file.size - file.lastValid);
    chunk.resize(toRead);
    std::fstream fs;
    fs.open(file.realPath, std::ios::in | std::ios::binary);
    if(!fs.is_open()) {
        return false;
    }
    fs.seekg(file.lastValid, std::ios::beg);

    fs.read(&chunk[0], toRead);
    if(fs.bad() || fs.tellg() != file.lastValid + toRead) {
        return false;
    }

    fs.close();

    file.lastValid += toRead;

    return true;
}

bool UserManager::getFileId(oid& ownerId, const string& filename, oid& res) {
    res = ownerId;
    return db.getIdById("files", "filename", filename, "owner", res);
}

bool UserManager::getFileIdAdvanced(oid& ownerId, const string& filename, const string& hash, oid& res) {
    res = ownerId;
    return db.getIdByDoc("files", make_document(
            kvp("owner", ownerId),
            kvp("hash", Database::stringToBinary(hash)),
            kvp("filename", bsoncxx::types::b_regex("/"+filename+"$"))
    ), res);
}

bool UserManager::shareWith(oid& fileId, oid& userId) {
    return db.pushValToArr("files", "sharedWith", fileId, make_document(kvp("userId", userId)));
}

bool UserManager::unshareWith(oid& fileId, oid& userId) {
    return db.removeFieldFromArray("files", "sharedWith", fileId, make_document(kvp("userId", userId)));
}

bool UserManager::listSharedWithUser(oid& id, vector<UFile>& list) {
    map<string, map<string, bsoncxx::types::value> > mmap;
    vector<string> fields;
    fields.emplace_back("filename");
    fields.emplace_back("size");
    fields.emplace_back("creationDate");
    fields.emplace_back("ownerName");
    fields.emplace_back("ownerUsername");
    fields.emplace_back("hash");
    fields.emplace_back("type");

    mongocxx::pipeline stages;

    stages.match(make_document(kvp("sharedWith.userId", id), kvp("isValid", true), kvp("type", FILE_REGULAR)));
    stages.lookup(make_document(kvp("from", "users"), kvp("localField", "owner"), kvp("foreignField", "_id"), kvp("as", "ownerTMP")));
    stages.unwind("$ownerTMP");
    stages.add_fields(make_document(kvp("ownerName", make_document(kvp("$concat", make_array("$ownerTMP.name", " ", "$ownerTMP.surname")))),
                                    kvp("ownerUsername", "$ownerTMP.username")));
    stages.project(make_document(kvp("ownerTMP", 0), kvp("_id", 0)));

    if(!db.getFieldsAdvanced("files", stages, fields, mmap)) {
        return false;
    }

    try {
        for(std::pair<string, map<string, bsoncxx::types::value> > usr: mmap) {
            UFile tmp;
            tmp.filename = usr.first.substr(usr.first.rfind('/') + 1);
            tmp.size = (uint64_t) usr.second.find("size")->second.get_int64();
            tmp.creation_date = (uint64_t) usr.second.find("creationDate")->second.get_date().to_int64();
            tmp.owner_name = bsoncxx::string::to_string(usr.second.find("ownerName")->second.get_utf8().value);
            tmp.owner_username = bsoncxx::string::to_string(usr.second.find("ownerUsername")->second.get_utf8().value);
            tmp.type = (uint8_t) usr.second.find("type")->second.get_int64().value;
            tmp.hash = string((const char*) usr.second.find("hash")->second.get_binary().bytes, usr.second.find("hash")->second.get_binary().size);
            tmp.isShared = true;

            list.emplace_back(tmp);
        }
    } catch (const std::exception& ex) {
        logger.err(l_id, "error while parsing shared file list: " + string(ex.what()));
        return false;
    } catch (...) {
        logger.err(l_id, "error while parsing shared file list: unknown error");
        return false;
    }

    return true;
}

bool UserManager::getFileFilename(oid& fileId, string& res) {
    return db.getField("files", "filename", fileId, res);
}

bool UserManager::updateLastChunkTime(UFile& file) {
    return db.setFieldCurrentDate("files", "lastChunkTime", file.id, file.lastChunkTime);
}

bool UserManager::removeAllUnfinishedForUser(oid& id) {
    vector<string> filesToDel;

    if(!db.getFieldM("files", "filename", make_document(
            kvp("isValid", false),
            kvp("type", FILE_REGULAR),
            kvp("owner", id)
    ), filesToDel)){
        return false;
    }

    logger.log(l_id, "deleting " + std::to_string(filesToDel.size()) + " unfinished files for user");

    for(auto& file: filesToDel) {
        if(!deleteFile(id, file)) {
            return false;
        }
    }

    return true;
}

bool UserManager::collectOldUnfinished() {
    std::chrono::system_clock::time_point curr = std::chrono::system_clock::now();
    std::chrono::system_clock::time_point thresholdTime
            = std::chrono::system_clock::time_point(curr - std::chrono::minutes(GARBAGE_COLLECTOR_TRESHOLD_MINUTES));

    map<string, map<string, bsoncxx::types::value> > mmap;

    if(!db.getFields("files", make_document(
            kvp("isValid", false),
            kvp("type", FILE_REGULAR),
            kvp("lastChunkTime", make_document(kvp("$lt", bsoncxx::types::b_date(thresholdTime))))
    ), vector<string>{"filename", "owner"}, mmap)){
        return false;
    }

    logger.info(l_id, "deleting " + std::to_string(mmap.size()) + " old unfinished files");

    for(auto& file: mmap) {
        oid id = file.second.find("owner")->second.get_oid().value;
        if(!deleteFile(id, file.first)) {
            return false;
        }
    }

    return true;
}

bool UserManager::getTotalSpace(oid& id, uint64_t& res) {
    return db.getField("users", "totalSpace", id, (int64_t&) res);
}

bool UserManager::getFreeSpace(oid& id, uint64_t& res) {
    return db.getField("users", "freeSpace", id, (int64_t&) res);
}

bool UserManager::setTotalSpace(oid& id, uint64_t& newVal) {
    return db.setField("users", "totalSpace", id, (int64_t&) newVal);
}

bool UserManager::changeFreeSpace(oid& id, int64_t diff) {
    return db.incField("users", id, "freeSpace", diff);
}


bool UserManager::shareInfo(oid& fileId, vector<string>& res) {
    mongocxx::pipeline stages;

    stages.match(make_document(kvp("_id", fileId)));
    stages.lookup(make_document(kvp("from", "users"), kvp("localField", "sharedWith.userId"), kvp("foreignField", "_id"), kvp("as", "sharedTMP")));
    stages.project(make_document(kvp("sharedWith", make_document(kvp("$map", make_document(
            kvp("input", "$sharedTMP"),
            kvp("as", "users"),
            kvp("in", "$$users.username")
    )))), kvp("_id", 0)));
    stages.unwind("$sharedWith");

    return db.getFieldMAdvanced("files", "sharedWith", stages, res);
}

bool UserManager::getWarningList(oid& id, vector<string>& list) {
    mongocxx::pipeline stages;

    stages.match(make_document(kvp("_id", id)));
    stages.project(make_document(kvp("warning", make_document(kvp("$map", make_document(
            kvp("input", "$warnings"),
            kvp("as", "warn"),
            kvp("in", "$$warn.body")
    )))), kvp("_id", 0)));
    stages.unwind("$warning");

    if(db.getFieldMAdvanced("users", "warning", stages, list)) {
        return db.setField("users", "warnings", id, bsoncxx::types::value{bsoncxx::types::b_array{}});
    }

    return false;
}

bool UserManager::addWarning(oid& id, const string& body) {
    return db.pushValToArr("users", "warnings", id, make_document(kvp("body", body)));
}

/**

db.getCollection('users').update(
{username: "miloszXD"},
{$push: {sids: {sid: "asdsasd", time: 12321}}})


db.getCollection('users').find({username: "miloszXD", "sids.sid": "asdsasd"})


db.getCollection('users').update({username: "miloszXD"}, {"$pull": {sids: {sid: "XDSID"}}})

**/
