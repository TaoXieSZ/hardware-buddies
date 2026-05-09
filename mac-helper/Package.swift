// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "BuddyHelper",
    platforms: [.macOS(.v13)],
    targets: [
        .executableTarget(
            name: "BuddyHelper",
            path: "Sources/BuddyHelper",
            resources: [.copy("Resources/Info.plist")]
        )
    ]
)
