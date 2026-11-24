import SwiftUI
import Foundation
import CryptoKit

@main struct PySNMobile: App {
    var body: some Scene { WindowGroup { ContentView() } }
}

enum Console: String, CaseIterable, Identifiable {
    case ps3 = "PS3", ps4 = "PS4", vita = "PS Vita", ps5 = "PS5"
    var id: String { rawValue }
}
struct Piece: Identifiable { let id = UUID(); let url: URL; let offset: Int64; let size: Int64; let hash: String? }
struct Update: Identifiable { let id = UUID(); let name: String; let version: String; let pieces: [Piece]; let filename: String; var size: Int64 { pieces.reduce(0) { $0 + $1.size } } }
struct Failure: LocalizedError { let message: String; var errorDescription: String? { message } }
func secureURL(_ value: String) throws -> URL {
    guard var c = URLComponents(string: value.trimmingCharacters(in: .whitespacesAndNewlines)), let host = c.host?.lowercased(), host.hasSuffix(".playstation.net"), c.user == nil, c.password == nil else { throw Failure(message: "Unexpected Sony download address") }
    c.scheme = "https"
    guard let u = c.url else { throw Failure(message: "Invalid URL") }; return u
}
func fetch(_ url: URL) async throws -> Data {
    let (data, response) = try await URLSession.shared.data(from: url)
    guard let http = response as? HTTPURLResponse, (200..<300).contains(http.statusCode) else { throw Failure(message: "Sony server returned an error or no update exists.") }; return data
}
final class XMLReader: NSObject, XMLParserDelegate {
    var packages: [[String:String]] = []; var images: [([String:String], String, String)] = []; var image: [String:String]?; var buffer = ""; var version = ""
    func parser(_ parser: XMLParser, didStartElement elementName: String, namespaceURI: String?, qualifiedName: String?, attributes: [String:String]) {
        if elementName == "package" || elementName == "delta_info_set" { packages.append(attributes) }
        if elementName == "system_pup" || elementName == "version" { version = attributes["label"] ?? version }
        if elementName == "image" { image = attributes; buffer = "" }
    }
    func parser(_ parser: XMLParser, foundCharacters string: String) { if image != nil { buffer += string } }
    func parser(_ parser: XMLParser, didEndElement elementName: String, namespaceURI: String?, qualifiedName: String?) { if elementName == "image", let a = image { images.append((a,buffer,version)); image = nil } }
    func read(_ data: Data) throws { let p = XMLParser(data: data); p.delegate = self; p.shouldResolveExternalEntities = false; guard p.parse() else { throw p.parserError ?? Failure(message:"Invalid XML") } }
}
func hex(_ s: String) -> Data { let a = Array(s); return Data(stride(from: 0, to: a.count, by: 2).compactMap { UInt8(String(a[$0...$0+1]), radix:16) }) }
func updates(console: Console, title: String) async throws -> [Update] {
    if console == .ps5 { throw Failure(message:"PS5 game updates are not supported by upstream PySN.") }
    let id = title.uppercased().trimmingCharacters(in:.whitespacesAndNewlines)
    guard id.range(of: "^[A-Z]{4}[0-9]{5}$", options:.regularExpression) != nil else { throw Failure(message:"Enter a title ID such as CUSA00935.") }
    var address = "https://a0.ww.np.dl.playstation.net/tpl/np/\(id)/\(id)-ver.xml"
    if console != .ps3 {
        let key = console == .ps4 ? "AD62E37F905E06BC19593142281C112CEC0E7EC3E97EFDCAEFCDBAAFA6378D84" : "E5E278AA1EE34082A088279C83F9BBC806821C52F2AB5D2B4ABD995450355114"
        let hash = HMAC<SHA256>.authenticationCode(for: Data("np_\(id)".utf8), using: SymmetricKey(data:hex(key))).map { String(format:"%02x",$0) }.joined()
        let path = console == .ps4 ? "plo" : "pl"
        address = "https://gs-sec.ww.np.dl.playstation.net/\(path)/np/\(id)/\(hash)/\(id)-ver.xml"
    }
    let xml = XMLReader(); try xml.read(await fetch(try secureURL(address)))
    var results: [Update] = []
    for a in xml.packages {
        let version = a["version"] ?? "Delta"
        if let manifest = a["manifest_url"] {
            let data = try await fetch(try secureURL(manifest))
            guard let root = try JSONSerialization.jsonObject(with:data) as? [String:Any], let list = root["pieces"] as? [[String:Any]] else { throw Failure(message:"Invalid PS4 manifest") }
            func number(_ x: Any?) -> Int64? { if let n = x as? NSNumber { return n.int64Value }; if let s = x as? String { return Int64(s) }; return nil }
            let pieces = try list.map { p -> Piece in
                guard let url = p["url"] as? String, let offset = number(p["fileOffset"]), let size = number(p["fileSize"]), size > 0 else { throw Failure(message:"Invalid package piece") }
                return Piece(url:try secureURL(url),offset:offset,size:size,hash:p["hashValue"] as? String)
            }.sorted { $0.offset < $1.offset }
            if let first = pieces.first { results.append(Update(name:id,version:version,pieces:pieces,filename:first.url.lastPathComponent.replacingOccurrences(of:"_0.pkg",with:".pkg"))) }
        } else if let url = a["url"] {
            let u = try secureURL(url); results.append(Update(name:id,version:version,pieces:[Piece(url:u,offset:0,size:Int64(a["size"] ?? "") ?? 0,hash:a["sha1sum"])],filename:u.lastPathComponent))
        }
    }; return results
}
func firmware(console: Console) async throws -> [Update] {
    let path: String
    switch console {
    case .ps3: path = "ps3/list/us/ps3-updatelist.txt"
    case .ps4: path = "ps4/list/us/ps4-updatelist.xml"
    case .vita: path = "psp2/list/us/psp2-updatelist.xml"
    case .ps5: path = "ps5/official/tJMRE80IbXnE9YuG0jzTXgKEjIMoabr6/list/us/updatelist.xml"
    }
    let host = console == .vita ? "psp2" : console.rawValue.lowercased()
    let data = try await fetch(try secureURL("https://fus01.\(host).update.playstation.net/update/\(path)"))
    if console == .ps3 {
        let text = String(decoding:data,as:UTF8.self)
        return try text.components(separatedBy:";").compactMap { item in
            guard let range = item.range(of:"https?://[^\\s;]+\\.PUP",options:.regularExpression) else { return nil }
            let u = try secureURL(String(item[range])); return Update(name:"PS3 Firmware",version:"US",pieces:[Piece(url:u,offset:0,size:0,hash:nil)],filename:u.lastPathComponent)
        }
    }
    let xml = XMLReader(); try xml.read(data)
    return try xml.images.compactMap { a, text, version in
        guard let range = text.range(of:"https?://[^\\s<>]+?\\.pup",options:[.regularExpression,.caseInsensitive]) else { return nil }
        let u = try secureURL(String(text[range])); return Update(name:console.rawValue + " Firmware",version:version,pieces:[Piece(url:u,offset:0,size:Int64(a["size"] ?? "") ?? 0,hash:nil)],filename:u.lastPathComponent)
    }
}
func save(_ update: Update, progress: @escaping (String)->Void) async throws -> URL {
    let fm = FileManager.default; let docs = try fm.url(for:.documentDirectory,in:.userDomainMask,appropriateFor:nil,create:true)
    let directory = docs.appendingPathComponent(update.id.uuidString,isDirectory:true); try fm.createDirectory(at:directory,withIntermediateDirectories:true)
    let destination = directory.appendingPathComponent(update.filename); let partial = directory.appendingPathComponent("download.partial")
    var done = false; defer { if !done { try? fm.removeItem(at:directory) } }
    guard fm.createFile(atPath:partial.path,contents:nil) else { throw Failure(message:"Cannot create output file") }
    let output = try FileHandle(forWritingTo:partial); defer { try? output.close() }; var offset: Int64 = 0
    for (i,piece) in update.pieces.enumerated() {
        guard piece.offset == offset else { throw Failure(message:"Non-contiguous package pieces") }
        progress("Downloading part \(i+1) of \(update.pieces.count)…")
        let (temp,response) = try await URLSession.shared.download(from:piece.url)
        defer { try? fm.removeItem(at:temp) }
        guard let http = response as? HTTPURLResponse, (200..<300).contains(http.statusCode) else { throw Failure(message:"Download failed") }
        let input = try FileHandle(forReadingFrom:temp); defer { try? input.close() }; var sha1 = Insecure.SHA1(); var sha256 = SHA256(); var count: Int64 = 0
        while let bytes = try input.read(upToCount:1024*1024), !bytes.isEmpty { try Task.checkCancellation(); sha1.update(data:bytes); sha256.update(data:bytes); try output.write(contentsOf:bytes); count += Int64(bytes.count) }
        if piece.size > 0 && count != piece.size { throw Failure(message:"File size mismatch") }
        if let expected = piece.hash, !expected.isEmpty {
            let actual: String
            if expected.count == 40 { actual = sha1.finalize().map { String(format:"%02x",$0) }.joined() }
            else if expected.count == 64 { actual = sha256.finalize().map { String(format:"%02x",$0) }.joined() }
            else { throw Failure(message:"Unsupported hash format") }
            guard actual.lowercased() == expected.lowercased() else { throw Failure(message:"Checksum mismatch") }
        }; offset += count
    }
    try output.synchronize(); try output.close(); try fm.moveItem(at:partial,to:destination); done = true; return destination
}
struct ContentView: View {
    @State private var console: Console = .ps4
    @State private var title = ""
    @State private var results: [Update] = []
    @State private var status = "Enter a title ID or search firmware."
    @State private var busy = false
    @State private var completed: [URL] = []
    var body: some View {
        NavigationStack {
            List {
                Section("Console") { Picker("Console",selection:$console) { ForEach(Console.allCases) { Text($0.rawValue).tag($0) } }.pickerStyle(.segmented)
                    TextField("Title ID (CUSA00935)",text:$title).textInputAutocapitalization(.characters).autocorrectionDisabled()
                    Button("Search game updates") { search(false) }.disabled(busy || console == .ps5)
                    Button("Search US firmware") { search(true) }.disabled(busy)
                }
                Section("Status") { if busy { ProgressView() }; Text(status).font(.footnote) }
                Section("Results") { ForEach(results) { item in VStack(alignment:.leading,spacing:8) { Text(item.name + " · " + item.version).font(.headline); Text(item.filename).font(.caption); if item.size > 0 { Text(ByteCountFormatter.string(fromByteCount:item.size,countStyle:.file)).font(.caption) }; Button("Download") { download(item) }.disabled(busy) } } }
                Section("Completed") { ForEach(completed,id:\.self) { url in ShareLink(item:url) { Label(url.lastPathComponent,systemImage:"square.and.arrow.up") } } }
            }.navigationTitle("PySN Mobile")
        }
    }
    private func search(_ fw: Bool) { busy = true; results = []; status = "Searching…"; let c = console; let t = title; Task { defer { busy = false }; do { results = try await (fw ? firmware(console:c) : updates(console:c,title:t)); status = results.isEmpty ? "No results found." : "\(results.count) result(s)." } catch { status = error.localizedDescription } } }
    private func download(_ item: Update) { busy = true; Task { defer { busy = false }; do { let url = try await save(item) { message in Task { @MainActor in status = message } }; completed.append(url); status = "Saved to Files. Checksums checked where supplied." } catch { status = error.localizedDescription } } }
}
