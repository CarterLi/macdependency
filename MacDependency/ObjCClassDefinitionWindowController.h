//
//  ObjCClassDefinitionWindowController.h
//  MacDependency
//

#import <Cocoa/Cocoa.h>

/**
 * A panel that shows the Objective-C declaration of one class.
 *
 * The text is read-only but selectable, so any part of the declaration can be
 * copied out of it; a button copies the whole thing at once.
 *
 * The window is built in code rather than in a nib, because it has no layout
 * of its own worth editing and that keeps it out of MyDocument.xib.
 */
@interface ObjCClassDefinitionWindowController : NSWindowController
{
	NSTextView* textView;
}

/** Creates the panel with the given window title, showing no text yet. */
- (id)initWithTitle:(NSString*)title;

/** The declaration to show, as produced by ObjCMetadata::getDefinition(). */
- (void)setDefinition:(NSString*)definition;

- (IBAction)copyDefinition:(id)sender;

@end
