//
//  MachOModel.h
//  MacDependency
//
//  Created by Konrad Windszus on 13.07.09.
//  Copyright 2009 Konrad Windszus. All rights reserved.
//

#import <Cocoa/Cocoa.h>
#import "ExtTreeModel.h"
#import "MachO/machoarchitecture.h"
#import "MachO/dylibcommand.h"
#import "MachO/macho.h"
#import "ArchitecturesController.h"

@class MyDocument;

enum State {
	StateNormal,
	StateWarning,
	StateError
};

@interface MachOModel : NSObject <ExtTreeModel> {
	NSMutableArray* children;
	MachO* file;
	MyDocument* document;
	MachOModel* parent;
	MachOArchitecture* architecture; // current architecture
	DylibCommand* command;
	State state;
}

- (id) initWithFile:(MachO*)machO document:(MyDocument*)document architecture:(MachOArchitecture*)architecture loadChildren:(BOOL)loadChildren; 
- (id) initWithFilename:(std::string&)filename command:(DylibCommand*)command document:(MyDocument*)document parent:(MachOModel*)parent;

- (NSArray*) children;
- (BOOL) isLeaf;
- (BOOL) isInMemory;
- (NSColor*) textColor;
- (NSString*) name;
- (NSNumber*) currentVersion;
- (NSNumber*) compatibleVersion;
- (NSString*) filename;
- (NSString*) dependencyType;

/**
 * What the file itself is: an executable, a dynamic library, or an image that
 * only lives in the dyld shared cache and so has no file of its own.
 *
 * The shared cache comes first of all, because that is the one thing about such
 * an image that is worth knowing before anything the header says: there is
 * nothing on disk to open, to reveal in the Finder or to hand to another tool.
 */
- (NSString*) fileKind;

- (NSString*) version;
- (NSDate*) lastModificationTime;
- (NSArray*) architectures;
- (NSNumber*) size;

@end
