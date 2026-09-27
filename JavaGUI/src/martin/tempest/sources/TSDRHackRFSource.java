/*******************************************************************************
 * Copyright (c) 2014 Martin Marinov.
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Public License v3.0
 * which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/gpl.html
 * 
 * Contributors:
 *     Martin Marinov - initial API and implementation
 ******************************************************************************/
package martin.tempest.sources;

import java.awt.Container;

/**
 * A source that works with a HackRF One (optionally with a PortaPack attached,
 * rebooted into "HackRF mode"). See <a href="https://greatscottgadgets.com/hackrf/">greatscottgadgets.com/hackrf</a>
 * 
 * @author Martin Marinov
 *
 */
public class TSDRHackRFSource extends TSDRSource {
	
	public TSDRHackRFSource() {
		super("HackRF One", "TSDRPlugin_HackRF", false);
	}
	
	@Override
	public boolean populateGUI(final Container cont, final String defaultprefs, final ActionListenerRegistrator okbutton) {
		setParams(defaultprefs);
		return false;
	}
}