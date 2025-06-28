package com.example.pogapp

import androidx.appcompat.app.AppCompatActivity
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import com.example.pogapp.databinding.ActivityMainBinding
import java.util.UUID
import kotlin.random.Random

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding

    private val pogStack = mutableListOf<Pog>()
    private val playerPogsCollection = mutableListOf<Pog>()
    private val opponentPogsCollection = mutableListOf<Pog>()

    private var playerPogsThisRound = 0
    private var opponentPogsThisRound = 0

    private var playerRoundsWon = 0
    private var opponentRoundsWon = 0
    private val roundsToWinMatch = 3 // Best 3 out of 5 means 3 rounds to win

    private lateinit var currentSlammer: Slammer

    private var isPlayerTurn = true
    private val initialPogCountPerPlayer = 5
    private var roundOver = false
    private var matchOver = false

    // AI difficulty - probability of AI flipping a pog (0.0 to 1.0)
    private val aiFlipProbability = 0.45 // Slightly less than player's 0.5

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        startNewMatch()

        binding.btnThrowSlammer.setOnClickListener {
            if (matchOver) {
                startNewMatch()
            } else if (roundOver) {
                setupNewRound()
            } else {
                if (isPlayerTurn) {
                    handleSlammerThrow(0.5) // Player has 50% chance per pog
                }
            }
        }
    }

    private fun startNewMatch() {
        matchOver = false
        playerRoundsWon = 0
        opponentRoundsWon = 0
        playerPogsCollection.clear()
        opponentPogsCollection.clear()
        setupNewRound()
        binding.tvGameMessage.text = "New Match! Best ${roundsToWinMatch} of ${(roundsToWinMatch * 2) -1 } wins. Player's turn."
    }


    private fun setupNewRound() {
        roundOver = false
        isPlayerTurn = true

        currentSlammer = Slammer(id = "default_slammer", weight = 50.0, material = SlammerMaterial.METAL)

        pogStack.clear()
        val roundWagerPogs = mutableListOf<Pog>()
        for (i in 1..(initialPogCountPerPlayer * 2)) {
            roundWagerPogs.add(Pog(id = UUID.randomUUID().toString()))
        }
        pogStack.addAll(roundWagerPogs)

        playerPogsThisRound = 0
        opponentPogsThisRound = 0

        binding.btnThrowSlammer.text = "Throw Slammer"
        // Ensure game message for new round is fresh or appended correctly
        val currentMessage = binding.tvGameMessage.text.toString()
        if (currentMessage.contains("New Match!")) { // Append if it's the first round of a match
             binding.tvGameMessage.append("\nNew Round! Stack: ${pogStack.size} pogs. Player's turn.")
        } else { // Else, set a new message for subsequent rounds
            binding.tvGameMessage.text = "New Round! Stack: ${pogStack.size} pogs. Player's turn."
        }
        updateUI()
    }

    private fun handleSlammerThrow(flipProbability: Double) {
        if (pogStack.isEmpty() || roundOver || matchOver) {
            return
        }

        // Make a mutable copy of the stack to iterate and modify `isFaceUp`
        val currentStackCopy = ArrayList(pogStack)
        var flippedInThisActionCount = 0

        for (pog in currentStackCopy) {
            if (Random.nextDouble() < flipProbability) {
                pog.isFaceUp = true // Mark as flipped
                flippedInThisActionCount++
            } else {
                pog.isFaceUp = false // Ensure it's marked correctly if not flipped
            }
        }

        val messageBuilder = StringBuilder()
        if (isPlayerTurn) {
            playerPogsThisRound += flippedInThisActionCount
            messageBuilder.append("Player flipped $flippedInThisActionCount pog(s)!")
        } else {
            opponentPogsThisRound += flippedInThisActionCount
            messageBuilder.append("Opponent flipped $flippedInThisActionCount pog(s)!")
        }

        // Update main pogStack: remove flipped pogs (those marked isFaceUp = true from currentStackCopy)
        // The original pogs that were in the stack for wagering are in `currentStackCopy`
        // The `pogStack` should now only contain the pogs that were NOT flipped.
        pogStack.clear()
        pogStack.addAll(currentStackCopy.filterNot { it.isFaceUp })
        pogStack.shuffle()


        binding.tvGameMessage.text = messageBuilder.toString()
        updateUI()

        if (pogStack.isEmpty()) {
            endRound(currentStackCopy) // Pass the original list of pogs that were in the stack for this turn
        } else {
            switchTurn()
        }
    }

    private fun switchTurn() {
        isPlayerTurn = !isPlayerTurn
        if (pogStack.isEmpty()) {
            return
        }

        binding.tvGameMessage.append("\n${if (isPlayerTurn) "Player" else "Opponent"}'s turn. Stack: ${pogStack.size} pogs.")
        binding.btnThrowSlammer.isEnabled = isPlayerTurn

        if (!isPlayerTurn) {
            Handler(Looper.getMainLooper()).postDelayed({
                if (!roundOver && !matchOver) {
                    handleSlammerThrow(aiFlipProbability) // AI uses its defined flip probability
                }
            }, 1500)
        }
    }

    private fun endRound(wageredPogsThisTurn: List<Pog>) {
        roundOver = true
        var roundWinnerMessage: String

        // Determine winner based on pogs collected THIS round
        if (playerPogsThisRound > opponentPogsThisRound) {
            playerRoundsWon++
            roundWinnerMessage = "Player wins this round! (${playerPogsThisRound} vs ${opponentPogsThisRound})"
            // "For keeps": Player gets all pogs that were in the stack at the start of this turn
            playerPogsCollection.addAll(wageredPogsThisTurn.map { it.copy() }) // Add copies to avoid issues if pogs are re-used
        } else if (opponentPogsThisRound > playerPogsThisRound) {
            opponentRoundsWon++
            roundWinnerMessage = "Opponent wins this round! (${opponentPogsThisRound} vs ${playerPogsThisRound})"
            opponentPogsCollection.addAll(wageredPogsThisTurn.map { it.copy() })
        } else {
            roundWinnerMessage = "It's a tie this round! (${playerPogsThisRound} vs ${opponentPogsThisRound})"
            // In a tie, wagered pogs could be returned to a general pool or split if possible.
            // For now, they are not awarded to either player's permanent collection on a tie.
        }

        binding.tvGameMessage.text = "Round Over! $roundWinnerMessage\nPlayer collected: $playerPogsThisRound, Opponent: $opponentPogsThisRound.\nPlayer total pogs: ${playerPogsCollection.size}, Opponent total pogs: ${opponentPogsCollection.size}"
        updateUI()

        if (playerRoundsWon >= roundsToWinMatch || opponentRoundsWon >= roundsToWinMatch) {
            endMatch()
        } else {
            binding.btnThrowSlammer.text = "Start Next Round"
            binding.btnThrowSlammer.isEnabled = true
        }
    }

    private fun endMatch() {
        matchOver = true
        val matchWinner = if (playerRoundsWon >= roundsToWinMatch) "Player" else "Opponent"
        binding.tvGameMessage.append("\n\n$matchWinner wins the match ${playerRoundsWon} to ${opponentRoundsWon}!")
        binding.tvGameMessage.append("\nFinal Pog Counts - Player: ${playerPogsCollection.size}, Opponent: ${opponentPogsCollection.size}")
        binding.btnThrowSlammer.text = "Start New Match"
        binding.btnThrowSlammer.isEnabled = true
        updateUI()
    }

    private fun updateUI() {
        binding.tvPlayerRoundScore.text = "Player Pogs (Round): $playerPogsThisRound"
        binding.tvOpponentRoundScore.text = "Opponent Pogs (Round): $opponentPogsThisRound"
        binding.tvPlayerMatchScore.text = "Player Rounds: $playerRoundsWon"
        binding.tvOpponentMatchScore.text = "Opponent Rounds: $opponentRoundsWon"

        binding.tvPogStackLabel.text = "Pog Stack (${pogStack.size})"
        binding.pogStackArea.setBackgroundColor(getColor(if (pogStack.isEmpty() || roundOver || matchOver) android.R.color.darker_gray else android.R.color.holo_blue_light))

        if (matchOver) {
            binding.btnThrowSlammer.text = "Start New Match"
            binding.btnThrowSlammer.isEnabled = true
        } else if (roundOver) {
            binding.btnThrowSlammer.text = "Start Next Round"
            binding.btnThrowSlammer.isEnabled = true
        } else {
            binding.btnThrowSlammer.text = "Throw Slammer"
            binding.btnThrowSlammer.isEnabled = isPlayerTurn
        }
    }
}
