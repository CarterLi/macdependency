//
//  ObjCClassDefinitionWindowController.m
//  MacDependency
//

#import "ObjCClassDefinitionWindowController.h"

static const CGFloat kInitialWidth = 760.0;
static const CGFloat kInitialHeight = 580.0;
static const CGFloat kMinimumWidth = 360.0;
static const CGFloat kMinimumHeight = 240.0;
static const CGFloat kButtonBarHeight = 40.0;
static const CGFloat kButtonHeight = 24.0;
static const CGFloat kButtonMargin = 12.0;

@implementation ObjCClassDefinitionWindowController

- (id)initWithTitle:(NSString*)title {
	NSRect frame = NSMakeRect(0.0, 0.0, kInitialWidth, kInitialHeight);
	NSUInteger styleMask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
	                       NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;

	NSWindow* window = [[NSWindow alloc] initWithContentRect:frame
	                                              styleMask:styleMask
	                                                backing:NSBackingStoreBuffered
	                                                  defer:NO];
	[window setTitle:title];
	[window setMinSize:NSMakeSize(kMinimumWidth, kMinimumHeight)];
	// The controller owns the window, so the window must not also try to own
	// itself: that would leave the controller with a dangling pointer.
	[window setReleasedWhenClosed:NO];

	self = [super initWithWindow:window];
	if (self) {
		NSView* contentView = [window contentView];
		NSRect bounds = [contentView bounds];

		NSScrollView* scrollView = [[NSScrollView alloc] initWithFrame:
			NSMakeRect(0.0, kButtonBarHeight, bounds.size.width, bounds.size.height - kButtonBarHeight)];
		[scrollView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
		[scrollView setHasVerticalScroller:YES];
		[scrollView setHasHorizontalScroller:YES];
		[scrollView setAutohidesScrollers:YES];
		[scrollView setBorderType:NSNoBorder];

		NSSize documentSize = [scrollView contentSize];
		textView = [[NSTextView alloc] initWithFrame:NSMakeRect(0.0, 0.0, documentSize.width, documentSize.height)];
		[textView setMinSize:NSMakeSize(0.0, 0.0)];
		[textView setMaxSize:NSMakeSize(FLT_MAX, FLT_MAX)];
		[textView setVerticallyResizable:YES];
		[textView setHorizontallyResizable:YES];
		[textView setAutoresizingMask:NSViewWidthSizable];
		[textView setFont:[NSFont userFixedPitchFontOfSize:12.0]];
		// Read only, but still selectable -- that is the whole point of this panel.
		[textView setEditable:NO];
		[textView setSelectable:YES];
		[textView setRichText:NO];
		[textView setUsesFindPanel:YES];
		[[textView textContainer] setContainerSize:NSMakeSize(FLT_MAX, FLT_MAX)];
		[[textView textContainer] setWidthTracksTextView:NO];
		[scrollView setDocumentView:textView];
		[contentView addSubview:scrollView];

		NSButton* copyButton = [[NSButton alloc] initWithFrame:
			NSMakeRect(kButtonMargin, kButtonMargin - 4.0, 0.0, kButtonHeight)];
		[copyButton setTitle:NSLocalizedString(@"OBJC_COPY_DEFINITION", nil)];
		[copyButton setBezelStyle:NSBezelStyleRounded];
		[copyButton setTarget:self];
		[copyButton setAction:@selector(copyDefinition:)];
		[copyButton setAutoresizingMask:NSViewMaxXMargin];
		[copyButton sizeToFit];
		[contentView addSubview:copyButton];

		NSButton* closeButton = [[NSButton alloc] initWithFrame:
			NSMakeRect(0.0, kButtonMargin - 4.0, 0.0, kButtonHeight)];
		[closeButton setTitle:NSLocalizedString(@"OBJC_CLOSE", nil)];
		[closeButton setBezelStyle:NSBezelStyleRounded];
		[closeButton setTarget:window];
		[closeButton setAction:@selector(performClose:)];
		[closeButton sizeToFit];
		NSRect closeFrame = [closeButton frame];
		closeFrame.origin.x = bounds.size.width - closeFrame.size.width - kButtonMargin;
		closeFrame.origin.y = kButtonMargin - 4.0;
		[closeButton setFrame:closeFrame];
		[closeButton setAutoresizingMask:NSViewMinXMargin];
		[contentView addSubview:closeButton];
	}
	return self;
}

- (void)showWindow:(id)sender {
	[super showWindow:sender];

	// So that the text can be selected (and copied) right away.
	[[self window] makeFirstResponder:textView];
}

- (void)setDefinition:(NSString*)definition {
	[textView setString:definition ? definition : @""];
	[textView scrollRangeToVisible:NSMakeRange(0, 0)];
}

- (IBAction)copyDefinition:(id)sender {
	NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
	[pasteboard declareTypes:[NSArray arrayWithObject:NSPasteboardTypeString] owner:nil];
	[pasteboard setString:[textView string] forType:NSPasteboardTypeString];
}

@end
