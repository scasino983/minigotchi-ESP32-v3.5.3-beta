package com.example.pogapp

import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.Robolectric
import org.robolectric.annotation.Config

// Required for testing Android components like Activity
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [Config.OLDEST_SDK]) // Configure for a specific SDK if needed
class MainActivityUnitTest {

    private lateinit var activity: MainActivity

    @Before
    fun setUp() {
        // Using Robolectric to create an instance of the Activity
        activity = Robolectric.buildActivity(MainActivity::class.java).create().get()
        // We need to call setupNewMatch to initialize game state for many tests
        // However, specific tests might want to manipulate state directly or call setupNewRound.
    }

    // Helper to access private fields via reflection for testing purposes ONLY.
    // In a real app, prefer making state observable or using ViewModel.
    private fun <T> MainActivity.getPrivateField(name: String): T {
        val field = MainActivity::class.java.getDeclaredField(name)
        field.isAccessible = true
        @Suppress("UNCHECKED_CAST")
        return field.get(this) as T
    }

    private fun <T> MainActivity.setPrivateField(name: String, value: T) {
        val field = MainActivity::class.java.getDeclaredField(name)
        field.isAccessible = true
        field.set(this, value)
    }

    private fun MainActivity.callPrivateMethod(name: String, vararg args: Any?) {
        val parameterTypes = args.map { it!!::class.javaPrimitiveType ?: it::class.java }.toTypedArray()
        val method = MainActivity::class.java.getDeclaredMethod(name, *parameterTypes)
        method.isAccessible = true
        method.invoke(this, *args)
    }
     private fun <R> MainActivity.callPrivateMethodWithReturn(name: String, vararg args: Any?): R {
        val parameterTypes = args.map { it!!::class.javaPrimitiveType ?: it::class.java }.toTypedArray()
        val method = MainActivity::class.java.getDeclaredMethod(name, *parameterTypes)
        method.isAccessible = true
        @Suppress("UNCHECKED_CAST")
        return method.invoke(this, *args) as R
    }


    @Test
    fun testNewMatchSetup() {
        activity.callPrivateMethod("startNewMatch")
        assertEquals(0, activity.getPrivateField<Int>("playerRoundsWon"))
        assertEquals(0, activity.getPrivateField<Int>("opponentRoundsWon"))
        assertEquals(false, activity.getPrivateField<Boolean>("matchOver"))
        assertFalse(activity.getPrivateField<MutableList<Pog>>("playerPogsCollection").isNotEmpty())
        assertFalse(activity.getPrivateField<MutableList<Pog>>("opponentPogsCollection").isNotEmpty())
    }

    @Test
    fun testNewRoundSetup() {
        // Call startNewMatch first to ensure context, then setupNewRound
        activity.callPrivateMethod("startNewMatch")
        activity.callPrivateMethod("setupNewRound")

        assertEquals(false, activity.getPrivateField<Boolean>("roundOver"))
        assertEquals(true, activity.getPrivateField<Boolean>("isPlayerTurn"))
        val pogStack = activity.getPrivateField<MutableList<Pog>>("pogStack")
        val initialPogCountPerPlayer = activity.getPrivateField<Int>("initialPogCountPerPlayer")
        assertEquals(initialPogCountPerPlayer * 2, pogStack.size)
        assertEquals(0, activity.getPrivateField<Int>("playerPogsThisRound"))
        assertEquals(0, activity.getPrivateField<Int>("opponentPogsThisRound"))
    }

    @Test
    fun testPlayerWinsRound_ForKeeps() {
        activity.callPrivateMethod("startNewMatch") // Resets collections and scores
        activity.callPrivateMethod("setupNewRound")

        val initialPogCountPerPlayer = activity.getPrivateField<Int>("initialPogCountPerPlayer")
        val wageredPogsCount = initialPogCountPerPlayer * 2

        // Simulate player winning all pogs in a round
        activity.setPrivateField("playerPogsThisRound", wageredPogsCount)
        activity.setPrivateField("opponentPogsThisRound", 0)

        // Create a dummy list of wagered pogs for the endRound method
        val dummyWageredPogs = List(wageredPogsCount) { Pog(id = "wagered_$it") }

        activity.callPrivateMethod("endRound", dummyWageredPogs)

        assertEquals(1, activity.getPrivateField<Int>("playerRoundsWon"))
        assertEquals(wageredPogsCount, activity.getPrivateField<MutableList<Pog>>("playerPogsCollection").size)
        assertEquals(0, activity.getPrivateField<MutableList<Pog>>("opponentPogsCollection").size)
        assertTrue(activity.getPrivateField<Boolean>("roundOver"))
    }

    @Test
    fun testOpponentWinsRound_ForKeeps() {
        activity.callPrivateMethod("startNewMatch")
        activity.callPrivateMethod("setupNewRound")
        val initialPogCountPerPlayer = activity.getPrivateField<Int>("initialPogCountPerPlayer")
        val wageredPogsCount = initialPogCountPerPlayer * 2

        activity.setPrivateField("playerPogsThisRound", 0)
        activity.setPrivateField("opponentPogsThisRound", wageredPogsCount)
        val dummyWageredPogs = List(wageredPogsCount) { Pog(id = "wagered_$it") }
        activity.callPrivateMethod("endRound", dummyWageredPogs)

        assertEquals(1, activity.getPrivateField<Int>("opponentRoundsWon"))
        assertEquals(wageredPogsCount, activity.getPrivateField<MutableList<Pog>>("opponentPogsCollection").size)
        assertEquals(0, activity.getPrivateField<MutableList<Pog>>("playerPogsCollection").size)
    }

    @Test
    fun testRoundTie_NoPogsAwardedToCollection() {
        activity.callPrivateMethod("startNewMatch")
        activity.callPrivateMethod("setupNewRound")
        val initialPogCountPerPlayer = activity.getPrivateField<Int>("initialPogCountPerPlayer")
        val wageredPogsCount = initialPogCountPerPlayer * 2

        activity.setPrivateField("playerPogsThisRound", wageredPogsCount / 2)
        activity.setPrivateField("opponentPogsThisRound", wageredPogsCount / 2)
        val dummyWageredPogs = List(wageredPogsCount) { Pog(id = "wagered_$it") }
        activity.callPrivateMethod("endRound", dummyWageredPogs)

        assertEquals(0, activity.getPrivateField<Int>("playerRoundsWon"))
        assertEquals(0, activity.getPrivateField<Int>("opponentRoundsWon"))
        assertEquals(0, activity.getPrivateField<MutableList<Pog>>("playerPogsCollection").size)
        assertEquals(0, activity.getPrivateField<MutableList<Pog>>("opponentPogsCollection").size)
    }

    @Test
    fun testPlayerWinsMatch() {
        activity.callPrivateMethod("startNewMatch")
        val roundsToWinMatch = activity.getPrivateField<Int>("roundsToWinMatch")

        for (i in 1..roundsToWinMatch) {
            activity.setPrivateField("playerPogsThisRound", 1) // Player wins round
            activity.setPrivateField("opponentPogsThisRound", 0)
            activity.callPrivateMethod("endRound", listOf(Pog("pog_$i")))
            if (activity.getPrivateField<Boolean>("matchOver")) break
            activity.callPrivateMethod("setupNewRound") // setup for next round if match not over
        }
        assertTrue(activity.getPrivateField<Boolean>("matchOver"))
        assertEquals(roundsToWinMatch, activity.getPrivateField<Int>("playerRoundsWon"))
    }

    // Note: Testing handleSlammerThrow directly is complex due to its direct UI updates
    // and reliance on pogStack state which is hard to control perfectly with Random.
    // More granular tests would involve mocking Random or refactoring flip logic.
    // This test is more of an integration test for the flip logic part.
    @Test
    fun testHandleSlammerThrow_PlayerAlwaysFlips() {
        activity.callPrivateMethod("startNewMatch")
        activity.callPrivateMethod("setupNewRound") // pogStack has initialPogCountPerPlayer * 2 pogs

        val pogStack = activity.getPrivateField<MutableList<Pog>>("pogStack")
        val initialStackSize = pogStack.size
        assertTrue(initialStackSize > 0)

        // Call handleSlammerThrow with 100% flip probability for player
        activity.callPrivateMethod("handleSlammerThrow", 1.0)

        assertEquals(initialStackSize, activity.getPrivateField<Int>("playerPogsThisRound"))
        assertEquals(0, activity.getPrivateField<MutableList<Pog>>("pogStack").size) // Stack should be empty
        assertTrue(activity.getPrivateField<Boolean>("roundOver")) // Round should end as stack is empty
    }

    @Test
    fun testHandleSlammerThrow_PlayerNeverFlips() {
        activity.callPrivateMethod("startNewMatch")
        activity.callPrivateMethod("setupNewRound")

        val pogStack = activity.getPrivateField<MutableList<Pog>>("pogStack")
        val initialStackSize = pogStack.size
        assertTrue(initialStackSize > 0)
        activity.setPrivateField("isPlayerTurn", true)

        // Call handleSlammerThrow with 0% flip probability
        activity.callPrivateMethod("handleSlammerThrow", 0.0)

        assertEquals(0, activity.getPrivateField<Int>("playerPogsThisRound"))
        assertEquals(initialStackSize, pogStack.size) // Stack should remain full
        assertFalse(activity.getPrivateField<Boolean>("roundOver")) // Round should not end
        assertFalse(activity.getPrivateField<Boolean>("isPlayerTurn")) // Turn should switch to opponent
    }
}
