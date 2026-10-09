//
//  MyDocument.m
//  MacDependency
//
//  Created by Konrad Windszus on 19.06.09.
//  Copyright Konrad Windszus 2009 . All rights reserved.
//

#import "MyDocument.h"
#import "ConversionStdString.h"
#import "MachOModel.h"
#import "TreeControllerExtension.h"
#import "ArchitectureModel.h"
#import "ObjCClassDefinitionWindowController.h"
#include "MachO/machoexception.h"
#include "MachO/objcmetadata.h"

#import <dlfcn.h>
#import <objc/runtime.h>

// The symbols that stand for an Objective-C class, and what they mean.
static NSString* const kObjCClassSymbolPrefix = @"_OBJC_CLASS_$_";
static NSString* const kObjCMetaclassSymbolPrefix = @"_OBJC_METACLASS_$_";

// declare private methods
@interface MyDocument ()
- (NSString*) serializeIndexPath:(NSIndexPath*)indexPath;
- (NSIndexPath*) deserializeIndexPath:(NSString*)link;
- (ObjCMetadata*) objcMetadataForArchitecture:(MachOArchitecture*)architecture;
- (NSTableView*) findSymbolsTableView;
- (NSOutlineView*) findDependenciesOutlineView;
- (void) definitionWindowWillClose:(NSNotification*)notification;
- (void) prepareDefinitionTextView;
- (SymbolTableEntryModel*) symbolModelAtRow:(NSInteger)row;
- (BOOL) objcClassName:(NSString**)className metaclass:(BOOL*)metaclass ofSymbol:(NSString*)symbol;
- (NSString*) definitionForClassName:(NSString*)className
                           metaclass:(BOOL)metaclass
                        architecture:(MachOArchitecture*)architecture;
- (void) setDefinitionInBottomTab:(NSString*)definition;
- (void) showObjCClassDefinitionWindow:(NSString*)className
                             metaclass:(BOOL)metaclass
                            definition:(NSString*)definition;
- (void) symbolSelectionDidChange:(NSNotification*)notification;
- (NSInteger) focusedSymbolRow;
- (NSString*) rowText:(NSInteger)row inTableView:(NSTableView*)tableView;
- (void) revealImportSymbol:(SymbolTableEntryModel*)model;
- (NSArray*) librariesOfSelectedImage;
- (MachOModel*) dependencyProvidingSymbolInProcess:(NSString*)symbol;
- (MachOModel*) dependencyWithFileNamed:(NSString*)path;
- (void) selectExportedSymbolNamed:(NSString*)name;
- (NSInteger) rowOfExportedSymbolNamed:(NSString*)name;
@end

@implementation MyDocument

- (id)init
{
    self = [super init];
    if (self) {

        // Add your subclass-specific initialization here.
        // If an error occurs here, send a [self release] message and return nil.
		//contents = [[NSMutableArray alloc] init];
		cache = new MachOCache();
		log = [[NSAttributedString alloc] initWithString:@""];
		numDependencies = 0;
		splitViewDelegate = [[PrioritySplitViewDelegate alloc] init];
		objcMetadata = 0;
		definitionWindows = [[NSMutableArray alloc] init];

    }
    return self;
}

- (void) dealloc
{
	[[NSNotificationCenter defaultCenter] removeObserver:self];

	if (objcMetadata != 0) {
		for (std::map<MachOArchitecture*, ObjCMetadata*>::iterator it = objcMetadata->begin(); it != objcMetadata->end(); ++it)
			delete it->second;
		delete objcMetadata;
		objcMetadata = 0;
	}
	delete cache;
}


- (NSString *)windowNibName
{
    // Override returning the nib file name of the document
    // If you need to use a subclass of NSWindowController or if your document supports multiple NSWindowControllers, you should remove this method and override -makeWindowControllers instead.
    return @"MyDocument";
}

- (void)windowControllerDidLoadNib:(NSWindowController *) aController
{
    [super windowControllerDidLoadNib:aController];

	// Add any code here that needs to be executed once the windowController has loaded the document's window.

	// Double clicking a symbol either opens the class definition of an
	// Objective-C class or takes you to the library that provides it; a single
	// click is enough for a class definition to show up in the bottom tab.
	// The table view is only a plain NSTableView in the nib, so the connections
	// are made here rather than in Interface Builder.
	NSTableView* tableView = symbolsTableView ? symbolsTableView : [self findSymbolsTableView];
	if (tableView != nil) {
		[tableView setTarget:self];
		[tableView setDoubleAction:@selector(showObjCClassDefinition:)];
		[[NSNotificationCenter defaultCenter] addObserver:self
		                                         selector:@selector(symbolSelectionDidChange:)
		                                             name:NSTableViewSelectionDidChangeNotification
		                                           object:tableView];
	}

	[self prepareDefinitionTextView];
}


- (NSData *)dataOfType:(NSString *)typeName error:(NSError **)outError
{
    // Insert code here to write your document to data of the specified type. If the given outError != NULL, ensure that you set *outError when returning nil.

    // You can also choose to override -fileWrapperOfType:error:, -writeToURL:ofType:error:, or -writeToURL:ofType:forSaveOperation:originalContentsURL:error: instead.

    // For applications targeted for Panther or earlier systems, you should use the deprecated API -dataRepresentationOfType:. In this case you can also choose to override -fileWrapperRepresentationOfType: or -writeToFile:ofType: instead.

    if ( outError != NULL ) {
		*outError = [NSError errorWithDomain:NSOSStatusErrorDomain code:unimpErr userInfo:NULL];
	}
	return nil;
}


- (BOOL)readFromURL:(NSURL *)absoluteURL ofType:(NSString *)typeName error:(NSError **)outError
{
	// TODO: detect changes on document file (FSEvents) or alternatively make reload possible

	// load file
	NSString* filePath = [absoluteURL path];

	// convert to std:string
	std::string fileString = [filePath stdString];
	try {
		machO = cache->getFile(fileString, NULL);
	} catch (MachOException& exc) {
		NSString* msg = [NSString stringWithUTF8String:exc.getCause().c_str()];


		// create and return custom domain error (the localized description key is overwritten by OS, so no point in setting it here)
		NSArray *objArray = [NSArray arrayWithObjects:@"", msg, @"Try again with another file", nil];
		NSArray *keyArray = [NSArray arrayWithObjects:NSLocalizedDescriptionKey, NSLocalizedFailureReasonErrorKey, NSLocalizedRecoverySuggestionErrorKey, nil];

		NSDictionary *eDict = [NSDictionary dictionaryWithObjects:objArray forKeys:keyArray];

		// fill outError
		*outError = [NSError errorWithDomain:@"MachO" code:0 userInfo:eDict];
		return NO;
	}
	return YES;
}

-(void)awakeFromNib {
	[[textFieldBottomBar cell] setBackgroundStyle:NSBackgroundStyleRaised];

	[splitViewDelegate setPriority:1 forViewAtIndex:0];
	[splitViewDelegate setPriority:0 forViewAtIndex:1];
	[splitViewDelegate setMinimumLength:150 forViewAtIndex:0];
	[splitViewDelegate setMinimumLength:400 forViewAtIndex:1];
	[mainSplitView setDelegate:splitViewDelegate];
}

- (NSAttributedString*)log {
	return log;
}

- (void)setLog:(NSAttributedString *)newLog {
	log = newLog;
}

- (void)clearLog {
	NSAttributedString* newLog = [[NSAttributedString alloc] initWithString:@""];
	[self setLog:newLog];
}

- (void)appendLogLine:(NSString *)line withModel:(MachOModel*)model state:(State)state {

	NSMutableAttributedString* newLog = [[NSMutableAttributedString alloc] init];
	[newLog appendAttributedString:log];

	NSString* prefix;
	switch (state) {
		case StateError:
			prefix = NSLocalizedString(@"LOG_PREFIX_ERROR", nil);
			break;
		default:
			prefix = NSLocalizedString(@"LOG_PREFIX_WARNING", nil);
			break;
	}

	NSString* newLine = [NSString stringWithFormat:@"%@%@\n\n", prefix, line];

	// A line that carries no colour of its own is drawn in black, whatever the
	// background is, and the log has a dark background once the system is in
	// dark mode. The dynamic colours are the ones that follow the appearance.
	NSColor* color = model != nil ? [NSColor linkColor] : [NSColor textColor];
	NSMutableDictionary* attributes = [NSMutableDictionary dictionaryWithObject:color
	                                                                    forKey:NSForegroundColorAttributeName];
	if (model) {
		[attributes setObject:model forKey:NSLinkAttributeName];
		[attributes setObject:[NSCursor pointingHandCursor] forKey:NSCursorAttributeName];
		[attributes setObject:NSLocalizedString(@"LOG_LINK_TOOLTIP", nil) forKey:NSToolTipAttributeName];
	}
	NSAttributedString* newLogLine = [[NSAttributedString alloc]initWithString:newLine attributes:attributes];

	[newLog appendAttributedString:newLogLine];
	[self setLog:newLog];
}

- (NSString*) workingDirectory {
	// don't release the returned string!!, apparently then filename is released also
    NSString* filePath = [[super fileURL] path];
	return [filePath stringByDeletingLastPathComponent];
}


- (NSString*) serializeIndexPath:(NSIndexPath*)indexPath {
	NSMutableString* link = [NSMutableString stringWithCapacity:20];
	for (int depth = 0; depth < [indexPath length]; depth++) {
		[link appendFormat: @"%ld;", [indexPath indexAtPosition: depth]];
	}
	return link;
}

- (NSIndexPath*) deserializeIndexPath:(NSString*)link {
	NSIndexPath* indexPath = nil;

	// tokenize string
	NSArray* indices = [link componentsSeparatedByString:@";"];

	// go through tokens
	NSEnumerator *enumerator = [indices objectEnumerator];
	NSString* token = [enumerator nextObject];
	if (token) {
		indexPath = [NSIndexPath indexPathWithIndex: [token intValue]];
		while ((token = [enumerator nextObject])) {
			if ([token length] > 0)
				indexPath = [indexPath indexPathByAddingIndex:[token intValue]];
		}
	}
	return indexPath;
}


// delegate method
- (BOOL)textView:(NSTextView *)aTextView clickedOnLink:(id)link atIndex:(NSUInteger)charIndex {
	[dependenciesController setSelectedObject: link];

	// we need no further processing of the link
	return YES;
}


- (MachOCache*)cache {
	return cache;
}

- (NSArray*) architectures {
	NSMutableArray* architectures = [NSMutableArray arrayWithCapacity:4];

	if (machO) {
		MachOArchitecture* architecture = machO->getHostCompatibleArchitecture();
		if (!architecture) {
			// no host-compatible architecture found (just take first architecture)
			architecture = *(machO->getArchitecturesBegin());
		}
		for (MachO::MachOArchitecturesIterator iter = machO->getArchitecturesBegin(); iter != machO->getArchitecturesEnd(); iter++) {
			// create model for architecture
			ArchitectureModel* currentArchitecture = [[ArchitectureModel alloc]initWithArchitecture:(*iter) file:machO document:self isRoot:YES];
			// correct order (current architecture should have first index)
			if ((*iter) == architecture) {
				[architectures insertObject:currentArchitecture atIndex:0]; // insert at beginning
			} else {
				[architectures addObject:currentArchitecture]; // insert at end
			}

		}
	}
	return  architectures;
}

- (IBAction)clickRevealInFinder:(id)sender {
	// The selected entry, and not the name the File Name field shows: for an
	// image served out of the dyld shared cache that name is the install name
	// of a library that is not a file, and the Finder would have nothing to
	// show. The entry knows the file the bytes really come from.
	MachOModel* model = [[dependenciesController selectedObjects] firstObject];
	NSString* filename = [model revealPath];
	if ([filename length] == 0)
		filename = [textFieldFilename stringValue];
	if ([filename length] == 0)
		return;

	[[NSWorkspace sharedWorkspace] selectFile: filename inFileViewerRootedAtPath: @""];

}

- (void)incrementNumDependencies {
	[self willChangeValueForKey:@"dependencyStatus"];
	numDependencies++;
	[self didChangeValueForKey:@"dependencyStatus"];
}

- (void)resetNumDependencies {
	[self willChangeValueForKey:@"dependencyStatus"];
	numDependencies = 0;
	[self didChangeValueForKey:@"dependencyStatus"];
}


- (NSString*)dependencyStatus {
	NSString* status = [NSString stringWithFormat:NSLocalizedString(@"DEPENDENCY_STATUS", nil), numDependencies, cache->getNumEntries()];
	return status;
}

- (SymbolTableController*)symbolTableController {
	return symbolTableController;
}

#pragma mark - Objective-C class definitions

- (ObjCMetadata*) objcMetadataForArchitecture:(MachOArchitecture*)architecture {
	if (objcMetadata == 0)
		objcMetadata = new std::map<MachOArchitecture*, ObjCMetadata*>();

	std::map<MachOArchitecture*, ObjCMetadata*>::iterator existing = objcMetadata->find(architecture);
	if (existing != objcMetadata->end())
		return existing->second;

	ObjCMetadata* metadata = new ObjCMetadata(*architecture);
	(*objcMetadata)[architecture] = metadata;
	return metadata;
}

/**
 * The symbol table view of the nib, found by walking the view hierarchy.
 *
 * Only used when the outlet is not connected -- the dependency tree is an
 * NSOutlineView, which is also an NSTableView, so it is skipped explicitly.
 */
- (NSTableView*) findSymbolsTableView {
	NSArray* controllers = [self windowControllers];
	if ([controllers count] == 0)
		return nil;
	NSView* root = [[[controllers objectAtIndex:0] window] contentView];
	if (root == nil)
		return nil;

	NSMutableArray* pending = [NSMutableArray arrayWithObject:root];
	while ([pending count] > 0) {
		NSView* view = [pending objectAtIndex:0];
		[pending removeObjectAtIndex:0];
		if ([view isKindOfClass:[NSOutlineView class]])
			continue;
		if ([view isKindOfClass:[NSTableView class]])
			return (NSTableView*)view;
		[pending addObjectsFromArray:[view subviews]];
	}
	return nil;
}

/** The dependency tree of the nib, found by walking the view hierarchy. */
- (NSOutlineView*) findDependenciesOutlineView {
	NSArray* controllers = [self windowControllers];
	if ([controllers count] == 0)
		return nil;
	NSView* root = [[[controllers objectAtIndex:0] window] contentView];
	if (root == nil)
		return nil;

	NSMutableArray* pending = [NSMutableArray arrayWithObject:root];
	while ([pending count] > 0) {
		NSView* view = [pending objectAtIndex:0];
		[pending removeObjectAtIndex:0];
		if ([view isKindOfClass:[NSOutlineView class]])
			return (NSOutlineView*)view;
		[pending addObjectsFromArray:[view subviews]];
	}
	return nil;
}

- (void) definitionWindowWillClose:(NSNotification*)notification {
	NSWindow* window = [notification object];
	[[NSNotificationCenter defaultCenter] removeObserver:self name:NSWindowWillCloseNotification object:window];

	// Index based, because the array is modified here.
	for (NSUInteger n = 0; n < [definitionWindows count]; n++) {
		ObjCClassDefinitionWindowController* controller = [definitionWindows objectAtIndex:n];
		if ([controller window] == window) {
			[definitionWindows removeObjectAtIndex:n];
			return;
		}
	}
}

/**
 * Gives the text view of the definition tab the behaviour that makes a class
 * declaration readable: monospaced, read only but selectable, and scrolling
 * sideways rather than wrapping the long protocol lists. It is the same setup
 * the class definition window does, only here it is applied to a view that the
 * nib owns.
 */
- (void) prepareDefinitionTextView {
	if (definitionTextView == nil)
		return;

	[definitionTextView setFont:[NSFont userFixedPitchFontOfSize:12.0]];
	[definitionTextView setEditable:NO];
	[definitionTextView setSelectable:YES];
	[definitionTextView setRichText:NO];
	[definitionTextView setUsesFindPanel:YES];
	[definitionTextView setVerticallyResizable:YES];
	[definitionTextView setHorizontallyResizable:YES];
	[definitionTextView setMaxSize:NSMakeSize(FLT_MAX, FLT_MAX)];
	[[definitionTextView textContainer] setContainerSize:NSMakeSize(FLT_MAX, FLT_MAX)];
	[[definitionTextView textContainer] setWidthTracksTextView:NO];
}

- (SymbolTableEntryModel*) symbolModelAtRow:(NSInteger)row {
	if (row < 0)
		return nil;
	NSArray* symbols = [symbolTableController arrangedObjects];
	if (row >= (NSInteger)[symbols count])
		return nil;
	id object = [symbols objectAtIndex:row];
	if (![object isKindOfClass:[SymbolTableEntryModel class]])
		return nil;
	return (SymbolTableEntryModel*)object;
}

/** Recognises the symbols that stand for an Objective-C class. */
- (BOOL) objcClassName:(NSString**)outClassName metaclass:(BOOL*)outMetaclass ofSymbol:(NSString*)symbol {
	NSString* prefix = nil;
	BOOL metaclass = NO;
	if ([symbol hasPrefix:kObjCClassSymbolPrefix]) {
		prefix = kObjCClassSymbolPrefix;
	} else if ([symbol hasPrefix:kObjCMetaclassSymbolPrefix]) {
		prefix = kObjCMetaclassSymbolPrefix;
		metaclass = YES;
	} else {
		return NO;
	}
	if (outClassName)
		*outClassName = [symbol substringFromIndex:[prefix length]];
	if (outMetaclass)
		*outMetaclass = metaclass;
	return YES;
}

- (NSString*) definitionForClassName:(NSString*)className
                           metaclass:(BOOL)metaclass
                        architecture:(MachOArchitecture*)architecture {
	if (architecture == 0)
		return nil;
	try {
		std::string text = [self objcMetadataForArchitecture:architecture]->getDefinition([className UTF8String], metaclass);
		if (!text.empty())
			return [NSString stringWithStdString:text];
	} catch (MachOException& exception) {
		[self appendLogLine:[NSString stringWithUTF8String:exception.getCause().c_str()] withModel:nil state:StateError];
	}
	return nil;
}

/**
 * Puts a text into the definition view of the bottom tab view.
 *
 * The tab is deliberately not selected here: a click in the symbol table
 * updates what the tab holds and nothing else, so a user who is reading the
 * warnings stays where they are.
 */
- (void) setDefinitionInBottomTab:(NSString*)definition {
	if (bottomTabView == nil || definitionTextView == nil)
		return;
	[definitionTextView setString:definition];
	[definitionTextView scrollRangeToVisible:NSMakeRange(0, 0)];
}

- (void) showObjCClassDefinitionWindow:(NSString*)className
                             metaclass:(BOOL)metaclass
                            definition:(NSString*)definition {
	NSString* titleFormat = metaclass ? NSLocalizedString(@"OBJC_METACLASS_WINDOW_TITLE", nil)
	                                  : NSLocalizedString(@"OBJC_CLASS_WINDOW_TITLE", nil);
	ObjCClassDefinitionWindowController* controller =
		[[ObjCClassDefinitionWindowController alloc] initWithTitle:[NSString stringWithFormat:titleFormat, className]];
	[controller setDefinition:definition];

	// Keep the panel alive while it is open, and let go of it once it closes.
	[[NSNotificationCenter defaultCenter] addObserver:self
	                                         selector:@selector(definitionWindowWillClose:)
	                                             name:NSWindowWillCloseNotification
	                                           object:[controller window]];
	[definitionWindows addObject:controller];
	[controller showWindow:self];
}

/**
 * A single click in the symbol table is enough to put the declaration of the
 * class the row stands for into the definition tab. Only what this image
 * defines has one; an imported symbol belongs to another library, and there the
 * double click is the one that has somewhere to go.
 *
 * A symbol that is not a class of this image at all -- a function, a string, a
 * class of another library -- has no declaration either, and saying so is more
 * useful than leaving the previous one standing.
 */
- (void) symbolSelectionDidChange:(NSNotification*)notification {
	NSTableView* tableView = [notification object];
	SymbolTableEntryModel* model = [self symbolModelAtRow:[tableView selectedRow]];
	if (model == nil)
		return;

	if ([model isImported])
		return;

	NSString* className = nil;
	BOOL metaclass = NO;
	if (![self objcClassName:&className metaclass:&metaclass ofSymbol:[model rawName]]) {
		[self setDefinitionInBottomTab:
			[NSString stringWithFormat:NSLocalizedString(@"OBJC_UNSUPPORTED_SYMBOL", nil), [model rawName]]];
		return;
	}

	NSString* definition = [self definitionForClassName:className
	                                          metaclass:metaclass
	                                       architecture:[model architecture]];
	if ([definition length] == 0)
		definition = [NSString stringWithFormat:NSLocalizedString(@"OBJC_NO_DEFINITION", nil), className];
	[self setDefinitionInBottomTab:definition];
}

- (IBAction)showObjCClassDefinition:(id)sender {
	NSTableView* tableView = symbolsTableView ? symbolsTableView : [self findSymbolsTableView];
	if (tableView == nil)
		return;

	NSInteger row = [tableView clickedRow];
	if (row < 0)
		row = [tableView selectedRow];

	SymbolTableEntryModel* model = [self symbolModelAtRow:row];
	if (model == nil)
		return;

	// A symbol this image does not define is provided by one of its
	// dependencies, so the double click goes there instead of showing a
	// declaration that is not in this image to begin with.
	if ([model isImported]) {
		[self revealImportSymbol:model];
		return;
	}

	NSString* className = nil;
	BOOL metaclass = NO;
	if (![self objcClassName:&className metaclass:&metaclass ofSymbol:[model rawName]])
		return;

	NSString* definition = [self definitionForClassName:className
	                                          metaclass:metaclass
	                                       architecture:[model architecture]];
	if ([definition length] == 0) {
		NSBeep();
		[self appendLogLine:[NSString stringWithFormat:NSLocalizedString(@"OBJC_NO_DEFINITION", nil), className]
		          withModel:nil state:StateWarning];
		return;
	}

	[self showObjCClassDefinitionWindow:className metaclass:metaclass definition:definition];
}

#pragma mark - Copying the symbol that is selected

/**
 * The row of the symbol table that is selected, and only while the table is
 * what has the focus.
 *
 * The Edit menu's Copy item is the document's to answer, and the document is
 * reached whenever nothing closer to the keyboard handles it -- so the symbol
 * table is checked for the focus rather than assumed to have it, which is what
 * keeps Copy from taking a symbol while the dependency tree is being used.
 */
- (NSInteger) focusedSymbolRow {
	NSTableView* tableView = symbolsTableView ? symbolsTableView : [self findSymbolsTableView];
	if (tableView == nil)
		return -1;

	NSArray* controllers = [self windowControllers];
	if ([controllers count] == 0)
		return -1;
	if ([[[controllers objectAtIndex:0] window] firstResponder] != tableView)
		return -1;

	return [tableView selectedRow];
}

/** The text of a row, written the way the table displays it: type, then name. */
- (NSString*) rowText:(NSInteger)row inTableView:(NSTableView*)tableView {
	NSMutableArray* parts = [NSMutableArray array];
	for (NSInteger column = 0; column < [tableView numberOfColumns]; column++) {
		// The cells carry the formatter of their column, so this is the text of
		// the row as it stands on screen rather than a second way of writing it.
		NSString* text = [[tableView preparedCellAtColumn:column row:row] stringValue];
		if ([text length] > 0)
			[parts addObject:text];
	}
	return [parts componentsJoinedByString:@" "];
}

- (IBAction)copy:(id)sender {
	NSTableView* tableView = symbolsTableView ? symbolsTableView : [self findSymbolsTableView];
	NSInteger row = [self focusedSymbolRow];
	if (tableView == nil || row < 0)
		return;

	NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
	[pasteboard declareTypes:[NSArray arrayWithObject:NSPasteboardTypeString] owner:nil];
	[pasteboard setString:[self rowText:row inTableView:tableView] forType:NSPasteboardTypeString];
}

/** The Copy item is only of use while the symbol table has something selected. */
- (BOOL)validateUserInterfaceItem:(id<NSValidatedUserInterfaceItem>)item {
	if ([item action] == @selector(copy:))
		return [self focusedSymbolRow] >= 0;
	return [super validateUserInterfaceItem:item];
}

#pragma mark - Following an imported symbol to the library that provides it

/** The dependencies of the image whose symbols the table is listing. */
- (NSArray*) librariesOfSelectedImage {
	NSArray* selected = [dependenciesController selectedObjects];
	if ([selected count] == 0)
		return nil;
	id image = [selected objectAtIndex:0];
	if (![image isKindOfClass:[MachOModel class]])
		return nil;
	return [(MachOModel*)image children];
}

/**
 * The dependency that provides an imported symbol.
 *
 * A symbol table entry names the library it is bound to by ordinal, and those
 * are the libraries of the load commands in order, which is the order the
 * dependency tree lists them in as well. Not every import carries one -- the
 * symbols a library only re-exports do not -- so those are looked up in this
 * process instead, where the runtime has already resolved them.
 */
- (MachOModel*) providerOfImportedSymbol:(SymbolTableEntryModel*)model {
	NSArray* libraries = [self librariesOfSelectedImage];
	unsigned int ordinal = [model libraryOrdinal];
	if (libraries != nil && ordinal >= 1 && ordinal <= [libraries count])
		return [libraries objectAtIndex:ordinal - 1];

	return [self dependencyProvidingSymbolInProcess:[model rawName]];
}

/**
 * The dependency that holds a symbol, as this process resolves it. Only a
 * library that is loaded can be found this way, which is exactly the case for
 * the libraries of a system image.
 */
- (MachOModel*) dependencyProvidingSymbolInProcess:(NSString*)symbol {
	void* address = 0;

	NSString* className = nil;
	BOOL metaclass = NO;
	if ([self objcClassName:&className metaclass:&metaclass ofSymbol:symbol]) {
		Class found = metaclass ? objc_getMetaClass([className UTF8String])
		                        : objc_getClass([className UTF8String]);
		address = (__bridge void*)found;
	} else if ([symbol hasPrefix:@"_"]) {
		// The symbol table holds the linker name; dlsym wants the C one.
		address = dlsym(RTLD_DEFAULT, [[symbol substringFromIndex:1] UTF8String]);
	}
	if (address == 0)
		return nil;

	Dl_info info;
	if (dladdr(address, &info) == 0 || info.dli_fname == 0)
		return nil;

	return [self dependencyWithFileNamed:[NSString stringWithUTF8String:info.dli_fname]];
}

/** The dependency that was read from the given file. */
- (MachOModel*) dependencyWithFileNamed:(NSString*)path {
	NSArray* libraries = [self librariesOfSelectedImage];
	if (libraries == nil)
		return nil;

	for (NSUInteger n = 0; n < [libraries count]; n++) {
		MachOModel* library = [libraries objectAtIndex:n];
		if ([[library filename] isEqualToString:path])
			return library;
	}

	// The same library can be named differently by the load command and by
	// dyld; the file name is what both agree on.
	NSString* name = [path lastPathComponent];
	for (NSUInteger n = 0; n < [libraries count]; n++) {
		MachOModel* library = [libraries objectAtIndex:n];
		if ([[[library filename] lastPathComponent] isEqualToString:name])
			return library;
	}
	return nil;
}

- (void) revealImportSymbol:(SymbolTableEntryModel*)model {
	MachOModel* provider = [self providerOfImportedSymbol:model];
	if (provider == nil) {
		NSBeep();
		[self appendLogLine:[NSString stringWithFormat:NSLocalizedString(@"OBJC_NO_PROVIDER", nil), [model rawName]]
		          withModel:nil state:StateWarning];
		return;
	}

	NSOutlineView* outlineView = [self findDependenciesOutlineView];

	// The library is a child of the image the symbol was imported by, so that
	// image has to be open for the library to be reachable.
	if (outlineView != nil) {
		NSInteger row = [outlineView selectedRow];
		if (row >= 0)
			[outlineView expandItem:[outlineView itemAtRow:row]];
	}

	if (![dependenciesController setSelectedObject:provider]) {
		NSBeep();
		[self appendLogLine:[NSString stringWithFormat:NSLocalizedString(@"OBJC_NO_PROVIDER", nil), [model rawName]]
		          withModel:nil state:StateWarning];
		return;
	}

	if (outlineView != nil)
		[outlineView scrollRowToVisible:[outlineView selectedRow]];

	// The symbol table follows the selection, but not until the array
	// controllers have rearranged themselves, so the export is picked up on the
	// next pass through the run loop.
	[self performSelector:@selector(selectExportedSymbolNamed:)
	           withObject:[model rawName]
	           afterDelay:0.0];
}

- (NSInteger) rowOfExportedSymbolNamed:(NSString*)name {
	NSArray* symbols = [symbolTableController arrangedObjects];
	for (NSUInteger n = 0; n < [symbols count]; n++) {
		id object = [symbols objectAtIndex:n];
		if (![object isKindOfClass:[SymbolTableEntryModel class]])
			continue;
		SymbolTableEntryModel* model = (SymbolTableEntryModel*)object;
		if ([model isImported])
			continue;
		if ([[model rawName] isEqualToString:name])
			return (NSInteger)n;
	}
	return -1;
}

/** Selects the row of an export, once the symbol table shows the new library. */
- (void) selectExportedSymbolNamed:(NSString*)name {
	NSTableView* tableView = symbolsTableView ? symbolsTableView : [self findSymbolsTableView];
	if (tableView == nil)
		return;

	// Only the exports of the library that was jumped to are of interest here,
	// so the type filter is narrowed to them before the row is looked for.
	[symbolTableController showExportedSymbols];

	NSInteger row = [self rowOfExportedSymbolNamed:name];
	if (row < 0) {
		NSBeep();
		[self appendLogLine:[NSString stringWithFormat:NSLocalizedString(@"OBJC_NO_EXPORT", nil), name]
		          withModel:nil state:StateWarning];
		return;
	}

	[tableView selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)row] byExtendingSelection:NO];
	[tableView scrollRowToVisible:row];
}

@end
